module carven:semantic.analysis.elaboration.stmt.binding.impl;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.stmt;
import :semantic.analysis.elaboration.types;
import :semantic.hir.decl;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import std;

namespace {

constexpr auto lower_binding_kind(ASTBindingKind kind) noexcept -> HIRBindingKind {
    switch (kind) {
        case ASTBindingKind::Let:   return HIRBindingKind::Let;
        case ASTBindingKind::Var:   return HIRBindingKind::Var;
        case ASTBindingKind::Const: break;
    }
    std::unreachable();
}
} // namespace
auto elaborate_binding(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    const ASTVariableDecl& declaration,
    Span statement_span,
    BodyControl control
) noexcept -> std::optional<HIRStmtID> {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    auto target = HIRBindingTarget {HIRDiscardBindingTarget {}};
    auto symbol = std::optional<SymbolID>();
    auto binding_name = std::optional<std::string_view>();
    auto binding_name_span = std::optional<Span>();
    if (const auto* named = std::get_if<ASTNamedBindingTarget>(&declaration.target)) {
        const auto name = module_analysis.spelling(named->name_span);
        binding_name = name;
        binding_name_span = named->name_span;
        symbol = module_analysis.append_symbol({
            .name = builder.intern_string(name),
            .module_id = std::nullopt,
            .role = SemanticSymbolRole::Local,
            .parent = std::nullopt,
        });
        builder.define_symbol_binding(
            *symbol,
            declaration.kind == ASTBindingKind::Const ? SemanticBindingRole::CompileTime
                                                      : SemanticBindingRole::Owner,
            declaration.kind == ASTBindingKind::Var
        );
        target = HIRNamedBindingTarget {
            .symbol = *symbol,
        };
    }
    const auto proof = declaration.kind == ASTBindingKind::Const
        ? std::optional(builder.begin_expression_proof())
        : std::nullopt;
    const auto declared_type = declaration.type.has_value()
        ? std::optional<HIRTypeID> {build_type(module_analysis, scopes, control, *declaration.type)}
        : std::nullopt;
    const auto failure_checkpoint = declaration.kind == ASTBindingKind::Const
        ? std::optional(module_analysis.error_checkpoint())
        : std::nullopt;
    const auto invalid_constant_initializer = declaration.kind == ASTBindingKind::Const
        && declaration.initializer.has_value()
        && constant_initializer_requires_body_scope(ast, *declaration.initializer);
    if (invalid_constant_initializer) {
        module_analysis.emit(
            ast.expression(*declaration.initializer).span,
            "const initializer is not a supported compile-time constant expression",
            DiagnosticCode::ConstInitializer
        );
    }
    const auto value = invalid_constant_initializer
        ? module_analysis.recover_expression(ast.expression(*declaration.initializer).span)
        : (declaration.initializer.has_value()
               ? (declared_type.has_value() ? build_expected_expression(
                                                  module_analysis,
                                                  scopes,
                                                  control,
                                                  *declaration.initializer,
                                                  *declared_type,
                                                  ExpectedExpressionUsage::Binding
                                              )
                                            : build_expression(
                                                  module_analysis,
                                                  scopes,
                                                  control,
                                                  *declaration.initializer
                                              ))
               : module_analysis.recover_expression(declaration.span));
    const auto binding_type = require_value_type(
        module_analysis,
        declared_type.value_or(expression_type(module_analysis, value)),
        declaration.type.has_value()
            ? ast.type(*declaration.type).span
            : (declaration.initializer.has_value() ? ast.expression(*declaration.initializer).span
                                                   : declaration.span),
        ValueTypeRole::Binding
    );
    if (symbol.has_value()) {
        set_symbol_type(module_analysis, *symbol, binding_type);
    }
    if (declaration.kind == ASTBindingKind::Const) {
        const auto constant = builder.expression(value).constant;
        if (!constant.has_value() && !module_analysis.error_observed_since(*failure_checkpoint)) {
            module_analysis.emit(
                declaration.initializer.has_value() ? ast.expression(*declaration.initializer).span
                                                    : declaration.span,
                "const initializer is not a supported compile-time constant expression",
                DiagnosticCode::ConstInitializer
            );
        }
        builder.finish_expression_proof(*proof);
        if (symbol.has_value() && constant.has_value()) {
            builder.define_symbol_constant(*symbol, *constant);
        }
    }
    if (symbol.has_value()) {
        if (!scopes.bind(*binding_name, *symbol)) {
            module_analysis.emit(
                *binding_name_span,
                "duplicate local name",
                DiagnosticCode::NameDuplicateLocal
            );
        } else {
            builder.record_symbol_lint_candidate(
                *symbol,
                module_analysis.origin(*binding_name_span)
            );
        }
    }
    if (declaration.kind == ASTBindingKind::Const) {
        return std::nullopt;
    }
    return builder.append_statement({
        .origin = module_analysis.origin(statement_span),
        .value = HIRBindingStmt {
            .kind = lower_binding_kind(declaration.kind),
            .target = std::move(target),
            .type = binding_type,
            .initializer = value,
        },
    });
}
