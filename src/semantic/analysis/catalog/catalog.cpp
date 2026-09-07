module carven:semantic.analysis.catalog.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :frontend.ast.ids;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.visibility;
import :source.cpp.identifier;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto declaration_name(const ASTItem& item) noexcept -> std::optional<Span> {
    return std::visit(
        Overloaded {
            [](const ASTEnumDecl& value) static noexcept -> std::optional<Span> {
                return value.name_span;
            },
            [](const ASTStructDecl& value) static noexcept -> std::optional<Span> {
                return value.name_span;
            },
            [](const ASTFunctionDecl& value) static noexcept -> std::optional<Span> {
                return value.name_span;
            },
            [](const ASTConstantDecl& value) static noexcept -> std::optional<Span> {
                return value.name_span;
            },
            [](const ASTTestDecl&) static noexcept -> std::optional<Span> { return std::nullopt; },
        },
        item.value
    );
}

auto semantic_visibility(const ASTDeclarationVisibility& visibility) noexcept
    -> DeclarationVisibility {
    return std::visit(
        Overloaded {
            [](const ASTPrivateDeclarationVisibility&) static noexcept {
                return DeclarationVisibility::Module;
            },
            [](const ASTBareDeclarationVisibility&) static noexcept {
                return DeclarationVisibility::ModuleDomain;
            },
            [](const ASTExportDeclarationVisibility&) static noexcept {
                return DeclarationVisibility::Compilation;
            },
        },
        visibility
    );
}

auto declaration_visibility(const ASTItem& item) noexcept -> DeclarationVisibility {
    return std::visit(
        Overloaded {
            [](const ASTEnumDecl& value) static noexcept {
                return semantic_visibility(value.visibility);
            },
            [](const ASTStructDecl& value) static noexcept {
                return semantic_visibility(value.visibility);
            },
            [](const ASTFunctionDecl& value) static noexcept {
                return semantic_visibility(value.visibility);
            },
            [](const ASTConstantDecl& value) static noexcept {
                return semantic_visibility(value.visibility);
            },
            [](const ASTTestDecl&) static noexcept { return DeclarationVisibility::Module; },
        },
        item.value
    );
}

auto catalog_error(SourceID source, std::string message, Span span) noexcept -> Diagnostic {
    return DiagnosticBuilder(DiagnosticCode::Catalog, std::move(message))
        .primary(locate(source, span))
        .build();
}

auto import_error(SourceID source, std::string message, Span span) noexcept -> Diagnostic {
    return DiagnosticBuilder(DiagnosticCode::ImportResolution, std::move(message))
        .primary(locate(source, span))
        .build();
}

auto find_symbol(
    const AnalysisCatalog& catalog,
    const CatalogModule& module_record,
    std::string_view name
) noexcept -> const CatalogSymbol* {
    for (const auto id : module_record.symbols) {
        const auto& symbol = catalog.view().symbols()[id.index()];
        if (symbol.name == name) {
            return std::addressof(symbol);
        }
    }
    return nullptr;
}

auto visible_to(
    const ProgramDraft& draft,
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    ProgramModuleID importer
) noexcept -> bool {
    switch (visibility) {
        case DeclarationVisibility::Module: return defining_module == importer;
        case DeclarationVisibility::ModuleDomain:
            return same_module_domain(
                draft.module_path_copy(defining_module),
                draft.module_path_copy(importer)
            );
        case DeclarationVisibility::Compilation: return true;
    }
    std::unreachable();
}

auto append_candidate(
    std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>& candidates,
    std::string_view name,
    CatalogLookupCandidate candidate
) noexcept -> void {
    candidates[std::string(name)].push_back(candidate);
}

auto contains_local_declaration(
    const std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>& candidates,
    const CatalogModule& module_record,
    std::string_view name
) noexcept -> bool {
    const auto found = candidates.find(name);
    return found != candidates.end()
        && std::ranges::any_of(found->second, [&](const auto& candidate) noexcept {
               return !candidate.import_binding.has_value()
                   && std::ranges::contains(module_record.symbols, candidate.symbol_id);
           });
}

auto is_explicit_import(
    const AnalysisCatalog& catalog,
    const CatalogLookupCandidate& candidate
) noexcept -> bool {
    if (!candidate.import_binding.has_value()) {
        return false;
    }
    const auto imports = catalog.view().imports();
    if (candidate.import_binding->index() >= imports.size()) {
        invariant_violation("catalog candidate references an unknown import binding");
    }
    return imports[candidate.import_binding->index()].selection_kind
        != CatalogImportSelectionKind::Wildcard;
}

auto has_explicit_candidate(
    const AnalysisCatalog& catalog,
    const std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>& candidates,
    std::string_view name
) noexcept -> bool {
    const auto found = candidates.find(name);
    if (found == candidates.end()) {
        return false;
    }
    return std::ranges::any_of(found->second, [&](const auto& candidate) noexcept {
        return is_explicit_import(catalog, candidate);
    });
}

auto remove_wildcard_candidates(
    const AnalysisCatalog& catalog,
    std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>& candidates,
    std::string_view name
) noexcept -> void {
    const auto found = candidates.find(name);
    if (found == candidates.end()) {
        return;
    }
    std::erase_if(found->second, [&](const auto& candidate) noexcept {
        return candidate.import_binding.has_value() && !is_explicit_import(catalog, candidate);
    });
}

auto duplicate_selection_error(
    SourceID source,
    Span duplicate,
    Span first,
    std::string message
) noexcept -> Diagnostic {
    auto diagnostic = import_error(source, std::move(message), duplicate);
    diagnostic.attachment.primary->message = "duplicate import selection";
    diagnostic.attachment.related.push_back({
        .span = locate(source, first),
        .message = "first selection",
    });
    return diagnostic;
}

} // namespace

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

auto AnalysisCatalogView::find_module(ProgramModuleID module_id) const noexcept
    -> const CatalogModule* {
    if (module_id.index() >= catalog->modules.size()
        || catalog->modules[module_id.index()].module_id != module_id) {
        return nullptr;
    }
    return std::addressof(catalog->modules[module_id.index()]);
}

auto AnalysisCatalogView::symbol(CatalogSymbolID id) const noexcept -> const CatalogSymbol* {
    return id.index() >= catalog->symbols.size() ? nullptr
                                                 : std::addressof(catalog->symbols[id.index()]);
}

auto AnalysisCatalogView::function_symbol(FunctionID id) const noexcept -> CatalogSymbolID {
    if (id.index() >= catalog->function_symbols.size()) {
        invariant_violation("catalog function lookup used an invalid identity");
    }
    const auto symbol = catalog->function_symbols[id.index()];
    const auto* form = std::get_if<CatalogFunctionForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->function != id) {
        invariant_violation("catalog function lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::struct_symbol(StructID id) const noexcept -> CatalogSymbolID {
    if (id.index() >= catalog->struct_symbols.size()) {
        invariant_violation("catalog struct lookup used an invalid identity");
    }
    const auto symbol = catalog->struct_symbols[id.index()];
    const auto* form = std::get_if<CatalogStructForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->structure != id) {
        invariant_violation("catalog struct lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::enum_symbol(EnumID id) const noexcept -> CatalogSymbolID {
    if (id.index() >= catalog->enum_symbols.size()) {
        invariant_violation("catalog enum lookup used an invalid identity");
    }
    const auto symbol = catalog->enum_symbols[id.index()];
    const auto* form = std::get_if<CatalogEnumForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->enumeration != id) {
        invariant_violation("catalog enum lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::enum_case_symbol(EnumCaseID id) const noexcept -> CatalogSymbolID {
    if (id.index() >= catalog->enum_case_symbols.size()) {
        invariant_violation("catalog enum-case lookup used an invalid identity");
    }
    const auto symbol = catalog->enum_case_symbols[id.index()];
    const auto* form = std::get_if<CatalogEnumCaseForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->enum_case != id) {
        invariant_violation("catalog enum-case lookup crossed semantic program owners");
    }
    return symbol;
}

auto AnalysisCatalogView::module_constant_symbol(ModuleConstantID id) const noexcept
    -> CatalogSymbolID {
    if (id.index() >= catalog->module_constant_symbols.size()) {
        invariant_violation("catalog module-constant lookup used an invalid identity");
    }
    const auto symbol = catalog->module_constant_symbols[id.index()];
    const auto* form = std::get_if<CatalogConstantForm>(&catalog->symbols[symbol.index()].form);
    if (form == nullptr || form->constant != id) {
        invariant_violation("catalog module-constant lookup crossed semantic program owners");
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

auto AnalysisCatalogView::cpp_imports(ProgramModuleID module_id) const noexcept
    -> std::span<const CatalogCppBinding> {
    if (module_id.index() >= catalog->modules.size()
        || catalog->modules[module_id.index()].module_id != module_id) {
        invariant_violation("C++ import lookup used an invalid module");
    }
    return catalog->cpp_bindings[module_id.index()];
}

auto build_analysis_catalog(ProgramDraft& draft) noexcept
    -> std::expected<AnalysisCatalog, Diagnostics> {
    auto result = AnalysisCatalog();
    auto diagnostics = Diagnostics();
    const auto syntax_trees = draft.syntax_trees();
    const auto module_count = draft.module_count();

    if (syntax_trees.size() != module_count) {
        invariant_violation("syntax trees are not aligned with compilation modules");
    }

    result.modules.reserve(module_count);
    result.visible_candidates.reserve(module_count);
    auto first_entry = std::optional<SourceSpan>();
    for (auto module_index = 0uz; module_index < module_count; ++module_index) {
        const auto module_id = draft.provenance_module_at(module_index);
        const auto source_id = syntax_trees[module_index].view().source_id();
        const auto ast = syntax_trees[module_index].view();
        const auto& ast_module = ast.ast_module();
        const auto declaration = draft.reserve_module_declaration();
        if (declaration.index() != module_index) {
            invariant_violation("semantic module reservation is not aligned with source modules");
        }
        auto catalog_module = CatalogModule {
            .module_id = module_id,
            .declaration = declaration,
            .symbols = {},
            .items = {},
        };
        auto names = std::flat_map<std::string, Span, std::less<>>();
        auto test_names = std::flat_map<std::string, Span, std::less<>>();
        auto local_candidates =
            std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>();
        for (const auto item_id : ast_module.items) {
            const auto& item = ast.item(item_id);
            if (const auto* function = std::get_if<ASTFunctionDecl>(&item.value)) {
                const auto function_name = draft.source_slice_copy(module_id, function->name_span);
                const auto is_cpp_import =
                    std::holds_alternative<ASTCppImportForm>(function->implementation);
                if (function_name == "main" && !is_cpp_import) {
                    const auto current = locate(source_id, function->name_span);
                    if (first_entry.has_value()) {
                        auto diagnostic = DiagnosticBuilder(
                            DiagnosticCode::EntryDuplicate,
                            "a program may define only one entry function"
                        );
                        diagnostic.primary(current, "duplicate entry");
                        diagnostic.related(*first_entry, "first entry");
                        diagnostics.push_back(diagnostic.build());
                    } else {
                        first_entry = current;
                    }
                }
            }
            if (const auto* test = std::get_if<ASTTestDecl>(&item.value)) {
                if (test->name == "main") {
                    diagnostics.push_back(DiagnosticBuilder(
                                              DiagnosticCode::TestMainName,
                                              "a test cannot be named 'main'"
                    )
                                              .primary(locate(source_id, test->name_span))
                                              .build());
                }
                const auto [position, inserted] = test_names.emplace(test->name, test->name_span);
                if (!inserted) {
                    auto diagnostic = DiagnosticBuilder(
                        DiagnosticCode::TestDuplicateName,
                        "a test name is defined more than once"
                    );
                    diagnostic.primary(locate(source_id, test->name_span), "duplicate test name");
                    diagnostic.related(locate(source_id, position->second), "first definition");
                    diagnostics.push_back(diagnostic.build());
                }
            }
            const auto name_span = declaration_name(item);
            if (!name_span.has_value()) {
                if (std::holds_alternative<ASTTestDecl>(item.value)) {
                    catalog_module.items.push_back({
                        .item_id = item_id,
                        .form = CatalogTestForm {.test = draft.reserve_test()},
                    });
                }
                continue;
            }
            auto name = draft.source_slice_copy(module_id, *name_span);
            if (const auto prior = names.find(name); prior != names.end()) {
                auto error = catalog_error(
                    source_id,
                    "a module declaration name is defined more than once",
                    *name_span
                );
                error.attachment.primary->message = "duplicate declaration";
                error.attachment.related.push_back({
                    .span = locate(source_id, prior->second),
                    .message = "first declaration",
                });
                diagnostics.push_back(std::move(error));
                continue;
            }
            names.emplace(name, *name_span);
            if (result.symbols.size() == std::numeric_limits<std::uint32_t>::max()) {
                resource_limit_exceeded("catalog symbols exhausted their 32-bit identity space");
            }
            const auto symbol_id =
                CatalogSymbolID::from_index(static_cast<std::uint32_t>(result.symbols.size()));
            auto form = std::visit(
                Overloaded {
                    [&](const ASTFunctionDecl&) noexcept -> CatalogSymbolForm {
                        const auto function = draft.reserve_function_declaration();
                        if (function.index() != result.function_symbols.size()) {
                            invariant_violation("semantic function reservation is not aligned");
                        }
                        result.function_symbols.push_back(symbol_id);
                        return CatalogFunctionForm {
                            .function = function,
                            .callable = draft.reserve_callable_declaration(),
                        };
                    },
                    [&](const ASTStructDecl&) noexcept -> CatalogSymbolForm {
                        const auto structure = draft.reserve_struct_declaration();
                        if (structure.index() != result.struct_symbols.size()) {
                            invariant_violation("semantic struct reservation is not aligned");
                        }
                        result.struct_symbols.push_back(symbol_id);
                        return CatalogStructForm {
                            .structure = structure,
                        };
                    },
                    [&](const ASTEnumDecl&) noexcept -> CatalogSymbolForm {
                        const auto enumeration = draft.reserve_enum_declaration();
                        if (enumeration.index() != result.enum_symbols.size()) {
                            invariant_violation("semantic enum reservation is not aligned");
                        }
                        result.enum_symbols.push_back(symbol_id);
                        return CatalogEnumForm {
                            .enumeration = enumeration,
                            .cases = {},
                        };
                    },
                    [&](const ASTConstantDecl&) noexcept -> CatalogSymbolForm {
                        const auto constant = draft.reserve_module_constant_declaration();
                        if (constant.index() != result.module_constant_symbols.size()) {
                            invariant_violation(
                                "semantic module-constant reservation is not aligned"
                            );
                        }
                        result.module_constant_symbols.push_back(symbol_id);
                        return CatalogConstantForm {.constant = constant};
                    },
                    [](const ASTTestDecl&) static noexcept -> CatalogSymbolForm {
                        invariant_violation("catalog declaration item has no symbol form");
                    },
                },
                item.value
            );
            result.symbols.push_back({
                .symbol_id = symbol_id,
                .module_id = module_id,
                .item_id = item_id,
                .name = std::move(name),
                .form = std::move(form),
                .visibility = declaration_visibility(item),
                .declaration_span = *name_span,
            });
            catalog_module.symbols.push_back(symbol_id);
            append_candidate(
                local_candidates,
                result.symbols[symbol_id.index()].name,
                {.symbol_id = symbol_id, .import_binding = std::nullopt}
            );

            std::visit(
                Overloaded {
                    [&](const CatalogFunctionForm& value) noexcept {
                        catalog_module.items.push_back({
                            .item_id = item_id,
                            .form = value.function,
                        });
                    },
                    [&](const CatalogStructForm& value) noexcept {
                        catalog_module.items.push_back({
                            .item_id = item_id,
                            .form = value.structure,
                        });
                    },
                    [&](const CatalogEnumForm& value) noexcept {
                        catalog_module.items.push_back({
                            .item_id = item_id,
                            .form = value.enumeration,
                        });
                    },
                    [&](const CatalogConstantForm& value) noexcept {
                        catalog_module.items.push_back({
                            .item_id = item_id,
                            .form = value.constant,
                        });
                    },
                    [](const CatalogEnumCaseForm&) static noexcept {
                        invariant_violation("enum case cannot be a module declaration item");
                    },
                },
                result.symbols[symbol_id.index()].form
            );

            const auto* enumeration = std::get_if<ASTEnumDecl>(&item.value);
            if (enumeration == nullptr) {
                continue;
            }
            for (auto case_index = 0uz; case_index < enumeration->cases.size(); ++case_index) {
                const auto& enum_case = enumeration->cases[case_index];
                if (result.symbols.size() == std::numeric_limits<std::uint32_t>::max()) {
                    resource_limit_exceeded(
                        "catalog symbols exhausted their 32-bit identity space"
                    );
                }
                const auto case_symbol =
                    CatalogSymbolID::from_index(static_cast<std::uint32_t>(result.symbols.size()));
                const auto& owner_form =
                    std::get<CatalogEnumForm>(result.symbols[symbol_id.index()].form);
                const auto case_id = draft.reserve_enum_case_declaration();
                if (case_id.index() != result.enum_case_symbols.size()) {
                    invariant_violation("semantic enum-case reservation is not aligned");
                }
                result.enum_case_symbols.push_back(case_symbol);
                result.symbols.push_back({
                    .symbol_id = case_symbol,
                    .module_id = module_id,
                    .item_id = item_id,
                    .name = draft.source_slice_copy(module_id, enum_case.name_span),
                    .form =
                        CatalogEnumCaseForm {
                            .enum_case = case_id,
                            .owner = owner_form.enumeration,
                            .index = static_cast<std::uint32_t>(case_index),
                        },
                    .visibility = result.symbols[symbol_id.index()].visibility,
                    .declaration_span = enum_case.name_span,
                });
                std::get<CatalogEnumForm>(result.symbols[symbol_id.index()].form)
                    .cases.push_back(case_id);
            }
        }
        auto cpp_bindings = std::vector<CatalogCppBinding>();
        auto cpp_selections = std::flat_map<std::string, std::vector<std::size_t>, std::less<>>();
        for (const auto [header_index, header] :
             std::views::enumerate(ast_module.cpp_header_imports)) {
            if (!header.using_clause) {
                continue;
            }
            const auto& clause = *header.using_clause;
            auto prefix = std::vector<std::string>();
            const auto spelling = [&](Span span) noexcept {
                auto name = draft.source_slice_copy(module_id, span);
                if (!is_supported_cpp_identifier(name)) {
                    diagnostics.push_back(
                        DiagnosticBuilder(
                            DiagnosticCode::CppIdentifier,
                            "external name cannot be represented as a C++ identifier"
                        )
                            .primary(locate(source_id, span))
                            .build()
                    );
                }
                return name;
            };
            for (const auto component : clause.prefix) {
                prefix.push_back(spelling(component));
            }
            const auto append = [&](Span leaf, bool opens_namespace) noexcept {
                auto components = prefix;
                if (!opens_namespace) {
                    components.push_back(spelling(leaf));
                    if (names.contains(components.back())) {
                        diagnostics.push_back(catalog_error(
                            source_id,
                            "a C++ import conflicts with a Carven declaration",
                            leaf
                        ));
                    }
                    auto& selections = cpp_selections[components.back()];
                    if (!selections.empty()
                        && cpp_bindings[selections.front()].components != components) {
                        diagnostics.push_back(catalog_error(
                            source_id,
                            "explicit C++ imports bind one name to different paths",
                            leaf
                        ));
                    }
                    selections.push_back(cpp_bindings.size());
                }
                cpp_bindings.push_back({
                    .header_index = static_cast<std::size_t>(header_index),
                    .components = std::move(components),
                    .opens_namespace = opens_namespace,
                    .origin = leaf,
                });
            };
            std::visit(
                Overloaded {
                    [&](const ASTCppSingleSelection& value) noexcept { append(value.name, false); },
                    [&](const ASTCppListSelection& value) noexcept {
                        for (const auto name : value.names) {
                            append(name, false);
                        }
                    },
                    [&](const ASTCppNamespaceSelection& value) noexcept {
                        append(value.star, true);
                    },
                },
                clause.selection
            );
        }
        result.cpp_bindings.push_back(std::move(cpp_bindings));
        result.cpp_selections.push_back(std::move(cpp_selections));
        result.modules.push_back(std::move(catalog_module));
        result.visible_candidates.push_back(std::move(local_candidates));
    }

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }

    for (auto module_index = 0uz; module_index < module_count; ++module_index) {
        const auto module_id = draft.provenance_module_at(module_index);
        const auto source_id = syntax_trees[module_index].view().source_id();
        const auto source_module_path = draft.module_path_copy(module_id);
        const auto ast = syntax_trees[module_index].view();
        const auto& ast_module = ast.ast_module();
        const auto& local_module = result.modules[module_index];
        auto& candidates = result.visible_candidates[module_index];

        struct ExplicitSelection final {
            CatalogSymbolID symbol;
            Span origin;
        };

        auto explicit_names = std::flat_map<std::string, ExplicitSelection, std::less<>>();
        auto wildcard_targets = std::flat_map<ProgramModuleID, ImportBindingID>();

        const auto closed_imports = draft.resolved_imports(module_id);
        if (closed_imports.size() != ast_module.module_imports.size()) {
            invariant_violation("resolved import row is not aligned with source imports");
        }
        for (const auto& closed_import : closed_imports) {
            const auto import_id = closed_import.declaration;
            const auto& module_import = ast.module_import(import_id);
            const auto& reference = module_import.module_reference;
            const auto target_id = closed_import.target;
            const auto& target_module = result.modules[target_id.index()];

            auto selected_names = std::vector<Span>();
            auto selection_kind = CatalogImportSelectionKind::Single;
            std::visit(
                [&]<typename Selection>(const Selection& selection) noexcept {
                    if constexpr (std::same_as<Selection, ASTSingleImport>) {
                        selected_names.push_back(selection.name_span);
                    } else if constexpr (std::same_as<Selection, ASTImportList>) {
                        selection_kind = CatalogImportSelectionKind::List;
                        selected_names.assign(selection.names.begin(), selection.names.end());
                    } else if constexpr (std::same_as<Selection, ASTWildcardImport>) {
                        selection_kind = CatalogImportSelectionKind::Wildcard;
                    } else {
                        static_assert(std::same_as<Selection, void>, "unhandled import selection");
                    }
                },
                module_import.selection.value
            );

            if (result.import_bindings.size() == std::numeric_limits<std::uint32_t>::max()) {
                resource_limit_exceeded("catalog imports exhausted their 32-bit identity space");
            }
            const auto binding_id = ImportBindingID::from_index(
                static_cast<std::uint32_t>(result.import_bindings.size())
            );
            result.import_bindings.push_back({
                .binding_id = binding_id,
                .importer = module_id,
                .declaration_id = import_id,
                .target = target_id,
                .declaration_span = module_import.span,
                .reference_span = reference.span,
                .selection_span = module_import.selection.span,
                .selection_kind = selection_kind,
                .selected_symbols = {},
            });

            auto names_in_declaration = std::flat_map<std::string, Span, std::less<>>();

            for (const auto name_span : selected_names) {
                const auto name = draft.source_slice_copy(module_id, name_span);
                if (const auto prior = names_in_declaration.find(name);
                    prior != names_in_declaration.end()) {
                    diagnostics.push_back(duplicate_selection_error(
                        source_id,
                        name_span,
                        prior->second,
                        std::format("imported name '{}' is selected more than once", name)
                    ));
                    continue;
                }
                names_in_declaration.emplace(std::string(name), name_span);
                const auto* symbol = find_symbol(result, target_module, name);
                if (symbol == nullptr) {
                    diagnostics.push_back(import_error(
                        source_id,
                        std::format(
                            "module '{}' has no declaration named '{}'",
                            draft.module_path_copy(target_id).value(),
                            name
                        ),
                        name_span
                    ));
                    continue;
                }
                if (!visible_to(draft, symbol->visibility, symbol->module_id, module_id)) {
                    diagnostics.push_back(import_error(
                        source_id,
                        std::format(
                            "declaration '{}' is not visible from module '{}'",
                            name,
                            source_module_path.value()
                        ),
                        name_span
                    ));
                    continue;
                }
                if (contains_local_declaration(candidates, local_module, name)) {
                    diagnostics.push_back(import_error(
                        source_id,
                        std::format("imported name '{}' conflicts with a module declaration", name),
                        name_span
                    ));
                    continue;
                }
                if (const auto prior = explicit_names.find(name); prior != explicit_names.end()) {
                    if (prior->second.symbol == symbol->symbol_id) {
                        diagnostics.push_back(duplicate_selection_error(
                            source_id,
                            name_span,
                            prior->second.origin,
                            std::format("imported name '{}' is selected more than once", name)
                        ));
                    } else {
                        auto diagnostic = import_error(
                            source_id,
                            std::format("imported name '{}' refers to more than one symbol", name),
                            name_span
                        );
                        diagnostic.attachment.related.push_back({
                            .span = locate(source_id, prior->second.origin),
                            .message = "first import selection",
                        });
                        diagnostics.push_back(std::move(diagnostic));
                    }
                    continue;
                }
                explicit_names.emplace(
                    std::string(name),
                    ExplicitSelection {.symbol = symbol->symbol_id, .origin = name_span}
                );
                result.import_bindings[binding_id.index()].selected_symbols.push_back({
                    .symbol_id = symbol->symbol_id,
                    .name = std::string(name),
                    .origin = name_span,
                });
                remove_wildcard_candidates(result, candidates, name);
                append_candidate(
                    candidates,
                    name,
                    {.symbol_id = symbol->symbol_id, .import_binding = binding_id}
                );
            }

            if (selection_kind != CatalogImportSelectionKind::Wildcard) {
                continue;
            }
            if (const auto prior = wildcard_targets.find(target_id);
                prior != wildcard_targets.end()) {
                const auto& first = result.import_bindings[prior->second.index()];
                diagnostics.push_back(duplicate_selection_error(
                    source_id,
                    module_import.selection.span,
                    first.selection_span,
                    std::format(
                        "module '{}' is imported by wildcard more than once",
                        draft.module_path_copy(target_id).value()
                    )
                ));
                continue;
            }
            wildcard_targets.emplace(target_id, binding_id);
            for (const auto symbol_id : target_module.symbols) {
                const auto& symbol = result.symbols[symbol_id.index()];
                if (!visible_to(draft, symbol.visibility, symbol.module_id, module_id)) {
                    continue;
                }
                result.import_bindings[binding_id.index()].selected_symbols.push_back({
                    .symbol_id = symbol_id,
                    .name = symbol.name,
                    .origin = module_import.selection.span,
                });
                if (contains_local_declaration(candidates, local_module, symbol.name)
                    || has_explicit_candidate(result, candidates, symbol.name)) {
                    continue;
                }
                append_candidate(
                    candidates,
                    symbol.name,
                    {.symbol_id = symbol_id, .import_binding = binding_id}
                );
            }
        }
    }

    for (auto module_index = 0uz; module_index < module_count; ++module_index) {
        for (const auto& binding : result.cpp_bindings[module_index]) {
            if (!binding.opens_namespace
                && result.visible_candidates[module_index].contains(binding.components.back())) {
                diagnostics.push_back(catalog_error(
                    syntax_trees[module_index].view().source_id(),
                    "a C++ import conflicts with a Carven binding",
                    binding.origin
                ));
            }
        }
    }
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return result;
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
