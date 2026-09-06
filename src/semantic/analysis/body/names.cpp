module carven:semantic.analysis.body.names.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.names;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {


auto BodyElaborator::select_name(const ASTNameExpr& name, Span span) noexcept
    -> AnalysisResult<SelectedExpression> {
    const auto text = spelling(name.name_span);
    if (const auto* local = use_local(text)) {
        if (const auto* constant = std::get_if<ConstantID>(&local->storage)) {
            auto value = active_builder().make_expression(
                local->type,
                active_builder().lifetime(),
                origin(span),
                SemConstant {.constant = *constant}
            );
            return BuiltExpression {
                .storage = std::move(value),

                .pending_failures = {},
                .takeable = false,
            };
        }
        const auto& runtime = std::get<BoundStorage>(local->storage);
        if (local->role == LocalRole::RangeRead) {
            auto value = active_builder().binding_expression(runtime.binding).expression;
            value.category = SemanticValueCategory::Value;
            value.lifetime = active_builder().lifetime();
            value.origin = origin(span);
            return BuiltExpression {
                .storage = std::move(value),

                .pending_failures = {},
                .takeable = false
            };
        }
        return BuiltExpression {
            .storage = active_builder().binding_expression(runtime.binding),

            .pending_failures = {},
            .takeable = local->takeable,
        };
    }
    if (catalog().lookup(source_module_id, text).empty()) {
        auto name_reference = lookup_cpp_name(
            draft(),
            catalog(),
            import_usage(),
            source_module_id,
            CppNameLookup::ModuleScope,
            std::span(&name.name_span, 1uz)
        );
        if (!name_reference.has_value()) {
            return std::unexpected(name_reference.error());
        }
        if (name_reference->has_value()) {
            return CppSelection {.target = std::move(**name_reference), .span = span};
        }
    }
    auto selected = find_global(text, name.name_span);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    return std::visit(
        Overloaded {
            [&](const CatalogFunctionForm& function) noexcept -> AnalysisResult<BuiltExpression> {
                auto value = active_builder().callable_expression(function.callable, origin(span));
                return BuiltExpression {
                    .storage = std::move(value),
                    .pending_failures = {},
                };
            },
            [&](const CatalogConstantForm& constant) noexcept -> AnalysisResult<BuiltExpression> {
                const auto declaration =
                    draft().module_constant_declaration_copy(constant.constant);
                auto value = active_builder().make_expression(
                    draft().constant_copy(declaration.value).type,
                    active_builder().lifetime(),
                    origin(span),
                    SemConstant {.constant = declaration.value}
                );
                return BuiltExpression {
                    .storage = std::move(value),

                    .pending_failures = {},
                    .takeable = false,
                };
            },
            [&](const CatalogEnumCaseForm& enum_case) noexcept -> AnalysisResult<BuiltExpression> {
                const auto type = draft().intern_type(
                    CanonicalType {
                        .value = EnumTypeValue {.enumeration = enum_case.owner},
                    }
                );
                return enum_case_reference(type, text, span, span);
            },
            [&]<typename Form>(const Form&) noexcept -> AnalysisResult<BuiltExpression> {
                static_assert(
                    std::same_as<Form, CatalogStructForm> || std::same_as<Form, CatalogEnumForm>,
                    "unhandled non-value catalog symbol"
                );
                return std::unexpected(fail(
                    name.name_span,
                    DiagnosticCode::TypeValueRequired,
                    std::format("'{}' does not name a runtime value", text)
                ));
            },
        },
        (*selected)->form
    );
}

} // namespace body_elaboration
