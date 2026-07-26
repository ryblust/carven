module carven:semantic.analysis.elaboration.types.symbol.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.hir.decl;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.invariant;
import std;

auto resolve_qualified_value(
    ModuleAnalysis& module_analysis,
    const ScopeStack& scopes,
    const ASTQualifiedName& qualified
) noexcept -> LookupResult<SymbolID> {
    auto& builder = module_analysis.builder();
    if (qualified.components.size() == 1) {
        const auto symbol = symbol_for(
            module_analysis,
            scopes,
            module_analysis.spelling(qualified.components.front()),
            qualified.components.front()
        );
        if (!symbol.has_value()) {
            return std::unexpected(symbol.error());
        }
        const auto symbol_id = symbol.value();
        const auto resolution =
            module_analysis.resolve_declaration(symbol_id, qualified.components.front());
        if (!resolution) {
            return std::unexpected(LookupError::Diagnosed);
        }
        return symbol_id;
    }
    if (qualified.components.size() != 2) {
        return std::unexpected(LookupError::Missing);
    }
    const auto parent = symbol_for(
        module_analysis,
        scopes,
        module_analysis.spelling(qualified.components.front()),
        qualified.components.front()
    );
    if (!parent.has_value()) {
        return std::unexpected(parent.error());
    }
    const auto parent_symbol = parent.value();
    const auto parent_resolution =
        module_analysis.resolve_declaration(parent_symbol, qualified.components.front());
    if (!parent_resolution) {
        return std::unexpected(LookupError::Diagnosed);
    }
    const auto* parent_catalog = module_analysis.catalog().symbol(parent_symbol);
    const auto* enumeration_form =
        parent_catalog == nullptr ? nullptr : std::get_if<CatalogEnumForm>(&parent_catalog->form);
    if (enumeration_form == nullptr) {
        return std::unexpected(LookupError::Missing);
    }
    const auto contract = module_analysis.declarations().enumeration(enumeration_form->enumeration);
    const auto case_name = module_analysis.spelling(qualified.components.back());
    const auto& cases = contract.cases;
    const auto found = std::ranges::find_if(cases, [&](EnumCaseID enum_case) noexcept {
        return builder.provenance().spelling(
                   module_analysis.declarations().enum_case(enum_case).name
               )
            == case_name;
    });
    if (found == cases.end()) {
        return std::unexpected(LookupError::Missing);
    }
    const auto case_symbol = module_analysis.declarations().enum_case(*found).symbol;
    const auto case_resolution =
        module_analysis.resolve_declaration(case_symbol, qualified.components.back());
    if (!case_resolution) {
        return std::unexpected(LookupError::Diagnosed);
    }
    return case_symbol;
}

auto resolve_enum_contract(ModuleAnalysis& module_analysis, HIRTypeID type_id, Span origin) noexcept
    -> LookupResult<EnumID> {
    const auto* nominal =
        std::get_if<HIREnumTypeValue>(&module_analysis.builder().type(type_id).value);
    if (nominal == nullptr) {
        return std::unexpected(LookupError::Missing);
    }
    const auto symbol = module_analysis.declarations().enumeration(nominal->enumeration).symbol;
    const auto resolution = module_analysis.resolve_declaration(symbol, origin);
    if (!resolution) {
        return std::unexpected(LookupError::Diagnosed);
    }
    return nominal->enumeration;
}

auto resolve_enum_case(
    ModuleAnalysis& module_analysis,
    HIRTypeID enum_type,
    std::string_view name,
    Span origin
) noexcept -> LookupResult<ResolvedEnumCase> {
    auto& builder = module_analysis.builder();
    const auto owner_resolution = resolve_enum_contract(module_analysis, enum_type, origin);
    if (!owner_resolution.has_value()) {
        return std::unexpected(owner_resolution.error());
    }
    const auto owner = owner_resolution.value();
    const auto contract = module_analysis.declarations().enumeration(owner);
    const auto found = std::ranges::find_if(contract.cases, [&](EnumCaseID candidate) noexcept {
        return builder.provenance().spelling(
                   module_analysis.declarations().enum_case(candidate).name
               )
            == name;
    });
    if (found == contract.cases.end()) {
        return std::unexpected(LookupError::Missing);
    }
    const auto enum_case = module_analysis.declarations().enum_case(*found);
    if (!module_analysis.resolve_declaration(enum_case.symbol, origin)) {
        return std::unexpected(LookupError::Diagnosed);
    }
    return ResolvedEnumCase {
        .id = *found,
        .symbol = enum_case.symbol,
        .payload_types = enum_case.payload_types,
        .constant = module_analysis.builder().symbol_constant(enum_case.symbol),
    };
}

auto resolve_structure_contract(
    ModuleAnalysis& module_analysis,
    HIRTypeID type_id,
    Span origin
) noexcept -> LookupResult<ResolvedStructure> {
    const auto* nominal =
        std::get_if<HIRStructTypeValue>(&module_analysis.builder().type(type_id).value);
    if (nominal == nullptr) {
        return std::unexpected(LookupError::Missing);
    }
    const auto contract = module_analysis.declarations().structure(nominal->structure);
    const auto symbol = contract.symbol;
    const auto resolution = module_analysis.resolve_declaration(symbol, origin);
    if (!resolution) {
        return std::unexpected(LookupError::Diagnosed);
    }
    return ResolvedStructure {.id = nominal->structure, .fields = contract.fields};
}

auto nominal_symbol(const ModuleAnalysis& module_analysis, HIRTypeID type_id) noexcept
    -> std::optional<SymbolID> {
    auto& builder = module_analysis.builder();
    if (const auto* structure = std::get_if<HIRStructTypeValue>(&builder.type(type_id).value)) {
        return module_analysis.catalog().struct_symbol(structure->structure);
    }
    if (const auto* enumeration = std::get_if<HIREnumTypeValue>(&builder.type(type_id).value)) {
        return module_analysis.catalog().enum_symbol(enumeration->enumeration);
    }
    return std::nullopt;
}

auto set_symbol_type(ModuleAnalysis& module_analysis, SymbolID symbol, HIRTypeID type) noexcept
    -> void {
    auto& builder = module_analysis.builder();
    builder.adopt_symbol_type(symbol, type);
}

auto symbol_type(ModuleAnalysis& module_analysis, SymbolID symbol) noexcept -> HIRTypeID {
    const auto& builder = module_analysis.builder();
    const auto type = builder.symbol(symbol).type;
    if (!type.has_value()) {
        invariant_violation("resolved symbol has no semantic type");
    }
    return *type;
}

auto symbol_for(
    ModuleAnalysis& module_analysis,
    const ScopeStack& scopes,
    std::string_view name,
    Span use_span
) noexcept -> LookupResult<SymbolID> {
    const auto catalog = module_analysis.catalog();
    const auto module_id = module_analysis.module_id();
    const auto local = scopes.find(name);
    if (local.symbol.has_value()) {
        if (!local.requires_capture) {
            return *local.symbol;
        }
        if (module_analysis.builder().symbol_constant(*local.symbol).has_value()) {
            return *local.symbol;
        }
        module_analysis.emit(
            use_span,
            std::format("runtime binding '{}' must be explicitly captured", name),
            DiagnosticCode::LambdaCaptureMissing
        );
        return std::unexpected(LookupError::Diagnosed);
    }
    const auto candidates = catalog.lookup(module_id, name);
    if (candidates.size() > 1) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::NameAmbiguous,
            std::format("name '{}' is provided by more than one wildcard import", name)
        );
        diagnostic.primary(locate(module_analysis.source_id(), use_span), "ambiguous reference");
        for (const auto candidate : candidates) {
            const auto* symbol = catalog.symbol(candidate.symbol_id);
            if (symbol == nullptr) {
                continue;
            }
            const auto& owner = catalog.module_record(symbol->module_id);
            const auto source_id = module_analysis.builder()
                                       .provenance()
                                       .source_snapshot(owner.source_id)
                                       .manager_source_id();
            diagnostic.related(
                locate(source_id, symbol->declaration_span),
                std::format("candidate from '{}'", owner.path.value())
            );
        }
        module_analysis.emit(diagnostic.build());
        return std::unexpected(LookupError::Diagnosed);
    }
    if (candidates.empty()) {
        return std::unexpected(LookupError::Missing);
    }
    const auto& selected = candidates.front();
    if (selected.import_binding.has_value()) {
        catalog.mark_import_used(*selected.import_binding);
    }
    return selected.symbol_id;
}
