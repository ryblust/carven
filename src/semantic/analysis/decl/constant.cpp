module carven:semantic.analysis.decl.constant.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.constant.proof;
import :semantic.analysis.decl;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

namespace decl_resolution {

auto DeclarationResolver::resolve_module_constant(
    const CatalogSymbol& symbol,
    const CatalogConstantForm& form,
    ASTView syntax,
    const ASTConstantDecl& declaration,
    Span item_span
) noexcept -> AnalysisResult<void> {
    if (symbol.visibility == DeclarationVisibility::Compilation && !declaration.type.has_value()) {
        return std::unexpected(fail(
            draft,
            symbol.module_id,
            declaration.name_span,
            DiagnosticCode::ConstExportedType,
            "exported constant requires an explicit declared type"
        ));
    }
    auto declared_type = std::optional<ConstructionTypeRef>();
    if (declaration.type.has_value()) {
        auto resolved = resolve_type(symbol.module_id, syntax, *declaration.type);
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        declared_type = *resolved;
    }
    const auto environment = constant_environment(symbol.module_id, syntax);
    auto proof = prove_constant_expression(
        draft,
        symbol.module_id,
        syntax,
        environment,
        declaration.initializer,
        declared_type
    );
    if (!proof.has_value()) {
        return std::unexpected(proof.error());
    }
    if (!proof->has_value()) {
        return std::unexpected(fail(
            draft,
            symbol.module_id,
            syntax.expression(declaration.initializer).span,
            DiagnosticCode::ConstInitializer,
            "constant initializer is not a supported compile-time value"
        ));
    }
    auto fact = std::move(**proof);
    const auto selected = declared_type.value_or(ConstructionTypeRef {fact.type});
    auto value_type = require_source_value_type(
        draft,
        selected,
        symbol.module_id,
        declaration.type.has_value() ? syntax.type(*declaration.type).span
                                     : syntax.expression(declaration.initializer).span,
        "constant"
    );
    if (!value_type.has_value()) {
        return std::unexpected(value_type.error());
    }
    const auto* concrete = std::get_if<TypeID>(&*value_type);
    if (concrete == nullptr) {
        return std::unexpected(fail(
            draft,
            symbol.module_id,
            syntax.expression(declaration.initializer).span,
            DiagnosticCode::ConstInitializer,
            "constant initializer is not a supported compile-time value"
        ));
    }
    fact.type = *concrete;
    if (form.constant.index() >= module_constants.size()) {
        invariant_violation("module constant identity is outside its reserved table");
    }
    module_constants[form.constant.index()] = ConstructionModuleConstantDeclaration {
        .module_id = module_declaration(symbol.module_id),
        .name = draft.intern_spelling(symbol.name),
        .origin = declaration_origin(draft, symbol.module_id, item_span),
        .visibility = symbol.visibility,
        .type = *value_type,
        .value = draft.intern_constant(std::move(fact)),
    };
    return {};
}

auto DeclarationResolver::resolve_constant_name(
    ProgramModuleID module,
    std::string_view name,
    Span origin
) noexcept -> AnalysisResult<ConstantNamedValue> {
    auto selected = select_symbol(module, name, origin);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    if (const auto* form = std::get_if<CatalogConstantForm>(&(*selected)->form)) {
        auto result = resolve((*selected)->symbol_id, module, origin);
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        const auto& declaration = module_constants[form->constant.index()];
        if (!declaration.has_value()) {
            invariant_violation("resolved module constant has no declaration fact");
        }
        return ConstantNamedValue {
            .type = declaration->type,
            .constant = declaration->value,
        };
    }
    return ConstantNamedValue {
        .type = draft.intern_builtin_type(BuiltinType::Void),
        .constant = std::nullopt,
    };
}

auto DeclarationResolver::resolve_enum_qualifier(
    ProgramModuleID module,
    ASTView syntax,
    ASTExprID expression
) noexcept -> AnalysisResult<std::optional<TypeID>> {
    auto current = expression;
    while (const auto* group = std::get_if<ASTGroupExpr>(&syntax.expression(current).value)) {
        current = group->expression;
    }
    const auto* name = std::get_if<ASTNameExpr>(&syntax.expression(current).value);
    if (name == nullptr) {
        return std::optional<TypeID>();
    }
    const auto spelling = draft.source_slice_copy(module, name->name_span);
    auto selected = select_symbol(module, spelling, name->name_span);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    const auto* enumeration = std::get_if<CatalogEnumForm>(&(*selected)->form);
    if (enumeration == nullptr) {
        return std::optional<TypeID>();
    }
    return std::optional(draft.intern_type(
        CanonicalType {
            .value = EnumTypeValue {.enumeration = enumeration->enumeration},
        }
    ));
}

auto DeclarationResolver::resolve_constant_enum_case(
    ProgramModuleID module,
    TypeID type,
    std::string_view name,
    Span origin
) noexcept -> AnalysisResult<ConstantEnumCase> {
    const auto canonical = draft.type_copy(type);
    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
    if (nominal == nullptr) {
        return std::unexpected(fail(
            draft,
            module,
            origin,
            DiagnosticCode::TypeEnumContext,
            "enum case qualifier does not name an enum type"
        ));
    }
    const auto owner_symbol_id = catalog.enum_symbol(nominal->enumeration);
    auto owner_result = resolve(owner_symbol_id, module, origin);
    if (!owner_result.has_value()) {
        return std::unexpected(owner_result.error());
    }
    const auto& owner_symbol = catalog_symbol(catalog, owner_symbol_id);
    const auto& owner_form = std::get<CatalogEnumForm>(owner_symbol.form);
    for (const auto case_id : owner_form.cases) {
        const auto& candidate = catalog_symbol(catalog, catalog.enum_case_symbol(case_id));
        if (candidate.name != name) {
            continue;
        }
        auto result = resolve(candidate.symbol_id, module, origin);
        if (!result.has_value()) {
            return std::unexpected(result.error());
        }
        const auto& declaration = enum_cases[case_id.index()];
        if (!declaration.has_value()) {
            invariant_violation("resolved enum case has no declaration fact");
        }
        return ConstantEnumCase {
            .id = case_id,
            .owner = declaration->owner,
            .payload_types = declaration->payload_types,
            .constant = declaration->constant,
        };
    }
    return std::unexpected(fail(
        draft,
        module,
        origin,
        DiagnosticCode::TypeMemberUnresolved,
        std::format("enum has no case named '{}'", name)
    ));
}

} // namespace decl_resolution
