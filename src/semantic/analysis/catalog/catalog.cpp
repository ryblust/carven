module carven:semantic.analysis.catalog.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :frontend.ast.ids;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.catalog;
import :semantic.visibility;
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

auto append_domain_prefix(
    std::vector<std::string_view>& components,
    const ModuleDomainPrefix& prefix
) noexcept -> void {
    if (const auto craft_name = prefix.craft_name()) {
        components.push_back("crafts");
        components.push_back(*craft_name);
    }
}

enum class ModuleReferenceResolutionError {
    InvalidCanonicalPath,
    EscapesModuleDomain,
};

auto resolve_path(
    SourceView source,
    const CanonicalModulePath& importer_path,
    const ASTModuleReference& reference
) noexcept -> std::expected<CanonicalModulePath, ModuleReferenceResolutionError> {
    auto components = std::vector<std::string_view>();
    std::visit(
        Overloaded {
            [&](const ASTDomainRootModuleReference& value) noexcept {
                append_domain_prefix(components, importer_path.module_domain_prefix());
                for (const auto component : value.components) {
                    components.push_back(slice(source.text, component));
                }
            },
            [&](const ASTParentRelativeModuleReference& value) noexcept {
                append_domain_prefix(components, importer_path.module_domain_prefix());
                const auto relative = importer_path.domain_relative_components();
                for (auto index = 0uz; index + 1 < relative.size(); ++index) {
                    components.push_back(relative[index]);
                }
                for (const auto component : value.components) {
                    components.push_back(slice(source.text, component));
                }
            },
            [&](const ASTCraftQualifiedModuleReference& value) noexcept {
                components.push_back("crafts");
                components.push_back(slice(source.text, value.name_span));
                for (const auto component : value.components) {
                    components.push_back(slice(source.text, component));
                }
            },
        },
        reference.value
    );
    auto resolved = CanonicalModulePath::from_components(components);
    if (!resolved.has_value()) {
        return std::unexpected(ModuleReferenceResolutionError::InvalidCanonicalPath);
    }
    const auto domain_local =
        !std::holds_alternative<ASTCraftQualifiedModuleReference>(reference.value);
    if (domain_local && !same_module_domain(importer_path, *resolved)) {
        return std::unexpected(ModuleReferenceResolutionError::EscapesModuleDomain);
    }
    return std::move(*resolved);
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

AnalysisCatalog::AnalysisCatalog(CompilationProvenanceView provenance) noexcept
    : compilation_provenance(provenance) {}

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

auto AnalysisCatalogView::module_record(ProgramModuleID module_id) const noexcept
    -> const ProgramModule& {
    return catalog->compilation_provenance.module_record(module_id);
}

auto AnalysisCatalogView::find_module(ProgramModuleID module_id) const noexcept
    -> const CatalogModule* {
    return module_id.index() >= catalog->modules.size()
        ? nullptr
        : std::addressof(catalog->modules[module_id.index()]);
}

auto AnalysisCatalogView::symbol(SymbolID id) const noexcept -> const CatalogSymbol* {
    return id.index() >= catalog->symbols.size() ? nullptr
                                                 : std::addressof(catalog->symbols[id.index()]);
}

auto AnalysisCatalogView::function_symbol(FunctionID id) const noexcept -> SymbolID {
    return catalog->function_symbols[id.index()];
}

auto AnalysisCatalogView::struct_symbol(StructID id) const noexcept -> SymbolID {
    return catalog->struct_symbols[id.index()];
}

auto AnalysisCatalogView::enum_symbol(EnumID id) const noexcept -> SymbolID {
    return catalog->enum_symbols[id.index()];
}

auto AnalysisCatalogView::enum_case_symbol(EnumCaseID id) const noexcept -> SymbolID {
    return catalog->enum_case_symbols[id.index()];
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
    if (module_id.index() >= catalog->visible_candidates.size()) {
        return {};
    }
    const auto& candidates = catalog->visible_candidates[module_id.index()];
    const auto named = candidates.find(name);
    return named == candidates.end() ? std::span<const CatalogLookupCandidate>()
                                     : std::span<const CatalogLookupCandidate>(named->second);
}

auto AnalysisCatalogView::mark_import_used(ImportBindingID binding) const noexcept -> void {
    if (binding.index() >= catalog->import_bindings.size()) {
        invariant_violation("semantic lookup selected an unknown import binding");
    }
    catalog->import_bindings[binding.index()].used = true;
}

auto build_analysis_catalog(
    CompilationProvenanceView provenance,
    std::span<const SyntaxTree> syntax_trees,
    SemanticEntityReservations reservations
) noexcept -> std::expected<AnalysisCatalog, Diagnostics> {
    auto result = AnalysisCatalog(provenance);
    auto diagnostics = Diagnostics();

    result.modules.reserve(provenance.module_records().size());
    result.visible_candidates.reserve(provenance.module_records().size());
    for (auto module_index = 0uz; module_index < provenance.module_records().size();
         ++module_index) {
        const auto module_id =
            ProgramModuleID::from_index(static_cast<std::uint32_t>(module_index));
        const auto& source_module = provenance.module_record(module_id);
        const auto& source_snapshot = provenance.source_snapshot(source_module.source_id);
        const auto ast = syntax_trees[module_index].view();
        const auto& ast_module = ast.ast_module();
        auto catalog_module = CatalogModule {
            .module_id = module_id,
            .symbols = {},
            .items = {},
        };
        auto names = std::flat_map<std::string, Span, std::less<>>();
        auto local_candidates =
            std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>();
        for (const auto item_id : ast_module.items) {
            const auto& item = ast.item(item_id);
            const auto name_span = declaration_name(item);
            if (!name_span.has_value()) {
                if (std::holds_alternative<ASTTestDecl>(item.value)) {
                    catalog_module.items.push_back({
                        .item_id = item_id,
                        .form = CatalogTestForm {},
                    });
                }
                continue;
            }
            auto name = std::string(source_snapshot.slice(*name_span));
            if (const auto prior = names.find(name); prior != names.end()) {
                auto error = catalog_error(
                    source_snapshot.manager_source_id(),
                    "a module declaration name is defined more than once",
                    *name_span
                );
                error.attachment.primary->message = "duplicate declaration";
                error.attachment.related.push_back({
                    .span = locate(source_snapshot.manager_source_id(), prior->second),
                    .message = "first declaration",
                });
                diagnostics.push_back(std::move(error));
                continue;
            }
            names.emplace(name, *name_span);
            const auto symbol_id = reservations.reserve_symbol();
            if (symbol_id.index() != result.symbols.size()) {
                invariant_violation("semantic symbol reservation is not append-aligned");
            }
            auto form = std::visit(
                Overloaded {
                    [&](const ASTFunctionDecl&) noexcept -> CatalogSymbolForm {
                        const auto function = reservations.reserve_function();
                        if (function.index() != result.function_symbols.size()) {
                            invariant_violation("semantic function reservation is not aligned");
                        }
                        result.function_symbols.push_back(symbol_id);
                        return CatalogFunctionForm {
                            .function = function,
                        };
                    },
                    [&](const ASTStructDecl&) noexcept -> CatalogSymbolForm {
                        const auto structure = reservations.reserve_struct();
                        if (structure.index() != result.struct_symbols.size()) {
                            invariant_violation("semantic struct reservation is not aligned");
                        }
                        result.struct_symbols.push_back(symbol_id);
                        return CatalogStructForm {
                            .structure = structure,
                        };
                    },
                    [&](const ASTEnumDecl&) noexcept -> CatalogSymbolForm {
                        const auto enumeration = reservations.reserve_enum();
                        if (enumeration.index() != result.enum_symbols.size()) {
                            invariant_violation("semantic enum reservation is not aligned");
                        }
                        result.enum_symbols.push_back(symbol_id);
                        return CatalogEnumForm {
                            .enumeration = enumeration,
                            .cases = {},
                        };
                    },
                    [](const ASTConstantDecl&) static noexcept -> CatalogSymbolForm {
                        return CatalogConstantForm {};
                    },
                    [](const auto&) static noexcept -> CatalogSymbolForm {
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
                    [](const CatalogConstantForm&) static noexcept {},
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
                const auto case_symbol = reservations.reserve_symbol();
                if (case_symbol.index() != result.symbols.size()) {
                    invariant_violation("semantic enum-case symbol reservation is not aligned");
                }
                const auto& owner_form =
                    std::get<CatalogEnumForm>(result.symbols[symbol_id.index()].form);
                const auto case_id = reservations.reserve_enum_case();
                if (case_id.index() != result.enum_case_symbols.size()) {
                    invariant_violation("semantic enum-case reservation is not aligned");
                }
                result.enum_case_symbols.push_back(case_symbol);
                result.symbols.push_back({
                    .symbol_id = case_symbol,
                    .module_id = module_id,
                    .item_id = item_id,
                    .name = std::string(source_snapshot.slice(enum_case.name_span)),
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
        result.modules.push_back(std::move(catalog_module));
        result.visible_candidates.push_back(std::move(local_candidates));
    }

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }

    for (auto module_index = 0uz; module_index < provenance.module_records().size();
         ++module_index) {
        const auto module_id =
            ProgramModuleID::from_index(static_cast<std::uint32_t>(module_index));
        const auto& source_module = provenance.module_record(module_id);
        const auto& source_snapshot = provenance.source_snapshot(source_module.source_id);
        const auto ast = syntax_trees[module_index].view();
        const auto source = SourceView {
            .source_id = source_snapshot.manager_source_id(),
            .text = source_snapshot.text(),
            .origin = source_snapshot.display_origin(),
        };
        const auto& ast_module = ast.ast_module();
        const auto& local_module = result.modules[module_index];
        auto& candidates = result.visible_candidates[module_index];
        struct ExplicitSelection final {
            SymbolID symbol;
            Span origin;
        };
        auto explicit_names = std::flat_map<std::string, ExplicitSelection, std::less<>>();
        auto wildcard_targets = std::flat_map<ProgramModuleID, ImportBindingID>();

        for (const auto import_id : ast_module.module_imports) {
            const auto& module_import = ast.module_import(import_id);
            const auto& reference = module_import.module_reference;
            const auto resolved = resolve_path(source, source_module.path, reference);
            if (!resolved.has_value()) {
                diagnostics.push_back(import_error(
                    source_snapshot.manager_source_id(),
                    resolved.error() == ModuleReferenceResolutionError::EscapesModuleDomain
                        ? "domain-local module reference escapes the importer's module domain"
                        : "module reference does not form a canonical path",
                    reference.span
                ));
                continue;
            }
            const auto target_id = provenance.find_program_module(*resolved);
            if (!target_id.has_value()) {
                diagnostics.push_back(import_error(
                    source_snapshot.manager_source_id(),
                    std::format(
                        "imported module '{}' is not present in this compilation batch",
                        resolved->value()
                    ),
                    reference.span
                ));
                continue;
            }
            if (*target_id == module_id) {
                diagnostics.push_back(import_error(
                    source_snapshot.manager_source_id(),
                    "a module cannot import itself",
                    reference.span
                ));
                continue;
            }
            const auto& target_module = result.modules[target_id->index()];

            auto selected_names = std::vector<Span>();
            auto selection_kind = CatalogImportSelectionKind::Single;
            std::visit(
                [&](const auto& selection) noexcept {
                    using T = std::decay_t<decltype(selection)>;
                    if constexpr (std::same_as<T, ASTSingleImport>) {
                        selected_names.push_back(selection.name_span);
                    } else if constexpr (std::same_as<T, ASTImportList>) {
                        selection_kind = CatalogImportSelectionKind::List;
                        selected_names.assign(selection.names.begin(), selection.names.end());
                    } else {
                        selection_kind = CatalogImportSelectionKind::Wildcard;
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
                .target = *target_id,
                .declaration_span = module_import.span,
                .reference_span = reference.span,
                .selection_span = module_import.selection.span,
                .selection_kind = selection_kind,
                .selected_symbols = {},
                .used = false,
            });

            auto names_in_declaration = std::flat_map<std::string, Span, std::less<>>();

            for (const auto name_span : selected_names) {
                const auto name = slice(source.text, name_span);
                if (const auto prior = names_in_declaration.find(name);
                    prior != names_in_declaration.end()) {
                    diagnostics.push_back(duplicate_selection_error(
                        source_snapshot.manager_source_id(),
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
                        source_snapshot.manager_source_id(),
                        std::format(
                            "module '{}' has no declaration named '{}'",
                            resolved->value(),
                            name
                        ),
                        name_span
                    ));
                    continue;
                }
                if (!declaration_visible_to(
                        symbol->visibility,
                        symbol->module_id,
                        module_id,
                        provenance
                    )) {
                    diagnostics.push_back(import_error(
                        source_snapshot.manager_source_id(),
                        std::format(
                            "declaration '{}' is not visible from module '{}'",
                            name,
                            source_module.path.value()
                        ),
                        name_span
                    ));
                    continue;
                }
                if (contains_local_declaration(candidates, local_module, name)) {
                    diagnostics.push_back(import_error(
                        source_snapshot.manager_source_id(),
                        std::format("imported name '{}' conflicts with a module declaration", name),
                        name_span
                    ));
                    continue;
                }
                if (const auto prior = explicit_names.find(name); prior != explicit_names.end()) {
                    if (prior->second.symbol == symbol->symbol_id) {
                        diagnostics.push_back(duplicate_selection_error(
                            source_snapshot.manager_source_id(),
                            name_span,
                            prior->second.origin,
                            std::format("imported name '{}' is selected more than once", name)
                        ));
                    } else {
                        auto diagnostic = import_error(
                            source_snapshot.manager_source_id(),
                            std::format("imported name '{}' refers to more than one symbol", name),
                            name_span
                        );
                        diagnostic.attachment.related.push_back({
                            .span =
                                locate(source_snapshot.manager_source_id(), prior->second.origin),
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
            if (const auto prior = wildcard_targets.find(*target_id);
                prior != wildcard_targets.end()) {
                const auto& first = result.import_bindings[prior->second.index()];
                diagnostics.push_back(duplicate_selection_error(
                    source_snapshot.manager_source_id(),
                    module_import.selection.span,
                    first.selection_span,
                    std::format(
                        "module '{}' is imported by wildcard more than once",
                        resolved->value()
                    )
                ));
                continue;
            }
            wildcard_targets.emplace(*target_id, binding_id);
            for (const auto symbol_id : target_module.symbols) {
                const auto& symbol = result.symbols[symbol_id.index()];
                if (!declaration_visible_to(
                        symbol.visibility,
                        symbol.module_id,
                        module_id,
                        provenance
                    )) {
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

    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    return result;
}
