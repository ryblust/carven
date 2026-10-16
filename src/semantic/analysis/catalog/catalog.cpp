module carven:semantic.analysis.catalog.impl;

import :frontend.ast.decl;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.visibility;
import :source.module_path;
import :source.standard;
import :support.invariant;
import std;

auto AnalysisCatalog::view() const noexcept -> AnalysisCatalogView {
    return AnalysisCatalogView(*this);
}

AnalysisCatalogView::AnalysisCatalogView(const AnalysisCatalog& catalog) noexcept
    : catalog(&catalog) {}

auto AnalysisCatalogView::modules() const noexcept -> std::span<const CatalogModule> {
    return catalog->modules;
}

auto AnalysisCatalogView::symbols() const noexcept -> std::span<const CatalogSymbol> {
    return catalog->symbols;
}

auto AnalysisCatalogView::imports() const noexcept -> std::span<const CatalogImportBinding> {
    return catalog->import_bindings;
}

auto AnalysisCatalogView::find_module(ProgramModuleID id) const noexcept -> const CatalogModule* {
    if (id.index() >= catalog->modules.size() || catalog->modules[id.index()].module_id != id) {
        return nullptr;
    }
    return std::addressof(catalog->modules[id.index()]);
}

auto AnalysisCatalogView::symbol(CatalogSymbolID id) const noexcept -> const CatalogSymbol* {
    return id.index() >= catalog->symbols.size() ? nullptr
                                                 : std::addressof(catalog->symbols[id.index()]);
}

auto AnalysisCatalogView::symbol(CatalogEntity entity) const noexcept -> const CatalogSymbol* {
    const auto* id = std::get_if<CatalogSymbolID>(&entity);
    return id ? symbol(*id) : nullptr;
}

auto AnalysisCatalogView::source_entity(
    const ProgramDraft& draft,
    CatalogEntity entity
) const noexcept -> SourceEntity {
    if (const auto* id = std::get_if<CatalogSymbolID>(&entity)) {
        return declaration_location(draft, *id);
    }
    return std::get<ProvidedExport>(entity);
}

auto AnalysisCatalogView::declaration_location(
    const ProgramDraft& draft,
    CatalogSymbolID id
) const noexcept -> SourceSpan {
    const auto* declaration = symbol(id);
    if (declaration == nullptr) {
        invariant_violation("catalog declaration location used an invalid symbol");
    }
    return locate(draft.module_source_id(declaration->module_id), declaration->declaration_span);
}

auto AnalysisCatalogView::field_location(
    const ProgramDraft& draft,
    StructID owner,
    std::size_t index
) const noexcept -> SourceSpan {
    const auto* declaration = symbol(struct_symbol(owner));
    if (draft.imported(declaration->module_id)) {
        const auto structure = draft.construction_struct_declaration_copy(owner);
        if (index >= structure.fields.size()) {
            invariant_violation("catalog field location used an invalid field");
        }
        return draft.source_span(structure.fields[index].origin);
    }
    const auto syntax = draft.syntax_tree(declaration->module_id).view();
    const auto* record = std::get_if<ASTRecordDecl>(&syntax.item(*declaration->item_id).value);
    if (record == nullptr || index >= record->fields.size()) {
        invariant_violation("catalog field location used an invalid field");
    }
    return locate(syntax.source_id(), record->fields[index].name_span);
}

auto AnalysisCatalogView::function_symbol(FunctionID id) const noexcept -> CatalogSymbolID {
    if (id.index() >= catalog->function_symbols.size() || !catalog->function_symbols[id.index()]) {
        invariant_violation("catalog function lookup used an invalid identity");
    }
    const auto symbol = *catalog->function_symbols[id.index()];
    const auto* form = std::get_if<CatalogFunctionForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->function != id) {
        invariant_violation("catalog function lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::generic_symbol(GenericDeclarationID id) const noexcept
    -> CatalogSymbolID {
    if (id.index() >= catalog->generic_symbols.size()) {
        invariant_violation("catalog generic lookup used an invalid identity");
    }
    const auto symbol = catalog->generic_symbols[id.index()];
    const auto* form = std::get_if<CatalogGenericForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->definition != id) {
        invariant_violation("catalog generic lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::struct_symbol(StructID id) const noexcept -> CatalogSymbolID {
    if (id.index() >= catalog->struct_symbols.size() || !catalog->struct_symbols[id.index()]) {
        invariant_violation("catalog struct lookup used an invalid identity");
    }
    const auto symbol = *catalog->struct_symbols[id.index()];
    const auto* form = std::get_if<CatalogStructForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->structure != id) {
        invariant_violation("catalog struct lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::enum_symbol(EnumID id) const noexcept -> CatalogSymbolID {
    if (id.index() >= catalog->enum_symbols.size() || !catalog->enum_symbols[id.index()]) {
        invariant_violation("catalog enum lookup used an invalid identity");
    }
    const auto symbol = *catalog->enum_symbols[id.index()];
    const auto* form = std::get_if<CatalogEnumForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->enumeration != id) {
        invariant_violation("catalog enum lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::enum_case_symbol(EnumCaseID id) const noexcept -> CatalogSymbolID {
    if (id.index() >= catalog->enum_case_symbols.size()
        || !catalog->enum_case_symbols[id.index()]) {
        invariant_violation("catalog enum-case lookup used an invalid identity");
    }
    const auto symbol = *catalog->enum_case_symbols[id.index()];
    const auto* form = std::get_if<CatalogEnumCaseForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->enum_case != id) {
        invariant_violation("catalog enum-case lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::function_count() const noexcept -> std::size_t {
    return catalog->function_symbols.size();
}

auto AnalysisCatalogView::struct_count() const noexcept -> std::size_t {
    return catalog->struct_symbols.size();
}

auto AnalysisCatalogView::enum_count() const noexcept -> std::size_t {
    return catalog->enum_symbols.size();
}

auto AnalysisCatalogView::enum_case_count() const noexcept -> std::size_t {
    return catalog->enum_case_symbols.size();
}

auto AnalysisCatalogView::lookup(ProgramModuleID module_id, std::string_view name) const noexcept
    -> std::span<const CatalogLookupCandidate> {
    if (module_id.index() >= catalog->visible_candidates.size()
        || module_id.index() >= catalog->modules.size()
        || catalog->modules[module_id.index()].module_id != module_id) {
        return {};
    }
    const auto& candidates = catalog->visible_candidates[module_id.index()];
    const auto named = candidates.find(name);
    return named == candidates.end() ? std::span<const CatalogLookupCandidate>()
                                     : std::span<const CatalogLookupCandidate>(named->second);
}

auto AnalysisCatalogView::visible_names(ProgramModuleID module_id) const noexcept
    -> std::vector<std::string> {
    if (module_id.index() >= catalog->visible_candidates.size()) {
        return {};
    }
    return std::ranges::to<std::vector<std::string>>(
        catalog->visible_candidates[module_id.index()].keys()
    );
}

auto AnalysisCatalogView::enum_case_names(EnumID enumeration) const noexcept
    -> std::vector<std::string> {
    auto names = std::vector<std::string>();
    const auto* owner = symbol(enum_symbol(enumeration));
    const auto* form = owner != nullptr ? std::get_if<CatalogEnumForm>(&owner->form) : nullptr;
    if (form == nullptr) {
        return names;
    }
    for (const auto case_id : form->cases) {
        if (const auto* declared = symbol(enum_case_symbol(case_id))) {
            names.push_back(declared->name);
        }
    }
    return names;
}

auto AnalysisCatalogView::cpp_selection(
    ProgramModuleID module_id,
    std::string_view name
) const noexcept -> std::span<const std::size_t> {
    static_cast<void>(cpp_imports(module_id));
    const auto& selections = catalog->cpp_selections[module_id.index()];
    const auto found = selections.find(name);
    return found == selections.end() ? std::span<const std::size_t>()
                                     : std::span<const std::size_t>(found->second);
}

auto AnalysisCatalogView::cpp_imports(ProgramModuleID id) const noexcept
    -> std::span<const CatalogCppBinding> {
    if (id.index() >= catalog->modules.size() || catalog->modules[id.index()].module_id != id) {
        invariant_violation("C++ import lookup used an invalid module");
    }
    return catalog->cpp_bindings[id.index()];
}

ImportUsage::ImportUsage(std::size_t import_count) noexcept
    : used_imports(import_count, std::uint8_t {0}) {}

auto ImportUsage::record(ImportBindingID import_id) noexcept -> void {
    if (import_id.index() >= used_imports.size()) {
        invariant_violation("import usage references an unknown binding");
    }
    used_imports[import_id.index()] = 1;
}

auto ImportUsage::was_used(ImportBindingID import_id) const noexcept -> bool {
    if (import_id.index() >= used_imports.size()) {
        invariant_violation("import usage references an unknown binding");
    }
    return used_imports[import_id.index()] != 0;
}

auto ImportUsage::record_cpp(ProgramModuleID module_id, Span origin) noexcept -> void {
    used_cpp_imports.emplace(module_id, origin.start());
}

auto ImportUsage::cpp_was_used(ProgramModuleID module_id, Span origin) const noexcept -> bool {
    return used_cpp_imports.contains({module_id, origin.start()});
}

auto AnalysisCatalogView::standard_declaration(
    const ProgramDraft& draft,
    StandardDeclaration declaration
) const noexcept -> const CatalogSymbol* {
    const auto& contract = ::standard_declaration(declaration);
    const auto module_id =
        draft.find_program_module(*CanonicalModulePath::from_value(contract.module));
    const auto* module = module_id ? find_module(*module_id) : nullptr;
    if (module == nullptr) {
        return nullptr;
    }
    for (const auto id : module->symbols) {
        const auto& symbol = catalog->symbols[id.index()];
        if (symbol.name != contract.name
            || symbol.visibility != DeclarationVisibility::Compilation) {
            continue;
        }
        const auto matches = (contract.kind == StandardDeclarationKind::Function
                              && std::holds_alternative<CatalogFunctionForm>(symbol.form))
            || (contract.kind == StandardDeclarationKind::Struct
                && std::holds_alternative<CatalogStructForm>(symbol.form))
            || (contract.kind == StandardDeclarationKind::Enum
                && std::holds_alternative<CatalogEnumForm>(symbol.form));
        return matches ? &symbol : nullptr;
    }
    return nullptr;
}
