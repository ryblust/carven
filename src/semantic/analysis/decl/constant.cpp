module carven:semantic.analysis.decl.constant.impl;

import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.operations;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.provenance.ids;
import :source.text;
import :support.invariant;
import std;

auto DeclResolver::resolve_module_constant(
    const CatalogSymbol& symbol,
    const CatalogConstantForm& form,
    ASTView syntax,
    const ASTConstantDecl& declaration,
    Span item_span
) noexcept -> AnalysisTask<void> {
    if (symbol.visibility == DeclarationVisibility::Compilation && !declaration.type.has_value()) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            declaration.name_span,
            DiagnosticCode::ConstExportedType,
            "exported constant requires an explicit declared type"
        ));
    }
    auto declared_type = std::optional<ConstructionTypeRef>();
    if (declaration.type.has_value()) {
        auto resolved = (co_await resolve_type(symbol.module_id, syntax, *declaration.type));
        if (!resolved.has_value()) {
            co_return std::unexpected(resolved.error());
        }
        declared_type = *resolved;
    }
    auto scope = ConstantScope {
        .resolver = *this,
        .module_id = symbol.module_id,
        .syntax = syntax,
        .generic_context = nullptr
    };
    auto result =
        (co_await scope.evaluate_constant_expression(declaration.initializer, declared_type));
    if (!result) {
        co_return std::unexpected(result.error());
    }
    if (!*result) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            syntax.expression(declaration.initializer).span,
            DiagnosticCode::ConstInitializer,
            "constant initializer is not a supported compile-time value"
        ));
    }
    const auto constant = **result;
    const auto& fact = draft.constant(constant);
    // Clang 23 can miscompile value_or with a fallback temporary in a coroutine,
    // retaining its resume-stack address across suspension. Keep this explicit
    // selection instead of value_or.
    const auto selected = declared_type ? *declared_type : ConstructionTypeRef {fact.type};
    const auto* concrete = std::get_if<TypeID>(&selected);
    if (concrete == nullptr) {
        co_return std::unexpected(declaration_failure(
            draft,
            symbol.module_id,
            syntax.expression(declaration.initializer).span,
            DiagnosticCode::ConstInitializer,
            "constant initializer is not a supported compile-time value"
        ));
    }
    if (fact.type != *concrete) {
        invariant_violation("constant declaration lost its evaluated contextual type");
    }
    if (form.constant.index() >= module_constants.size()) {
        invariant_violation("module constant identity is outside its reserved table");
    }
    if (auto* observations = draft.source_analysis()) {
        const auto name = locate(syntax.source_id(), declaration.name_span);
        observations->declarations().record(draft, name, name, fact.type);
    }
    module_constants[form.constant.index()] = ModuleConstantDeclaration {
        .module_id = module_declaration(symbol.module_id),
        .name = draft.intern_spelling(symbol.name),
        .origin = declaration_source_origin(draft, symbol.module_id, item_span),
        .visibility = symbol.visibility,
        .value = constant,
    };
    co_return {};
}
