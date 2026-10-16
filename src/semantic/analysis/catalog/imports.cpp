module carven:semantic.analysis.catalog.imports.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.catalog;
import :semantic.analysis.catalog.builder;
import :semantic.analysis.program;
import :semantic.visibility;
import :source.cpp.identifier;
import :source.module_path;
import :source.standard;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto import_error(SourceID source, std::string message, Span span) noexcept -> Diagnostic {
    return DiagnosticBuilder(DiagnosticCode::ImportResolution, std::move(message))
        .primary(locate(source, span))
        .build();
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

auto contains_local_declaration(
    const std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>& candidates,
    const CatalogModule& module_record,
    std::string_view name
) noexcept -> bool {
    const auto found = candidates.find(name);
    return found != candidates.end()
        && std::ranges::any_of(found->second, [&](const auto& candidate) noexcept {
               return !candidate.import_binding.has_value()
                   && (std::holds_alternative<ProvidedExport>(candidate.entity)
                       || std::ranges::contains(
                           module_record.symbols,
                           std::get<CatalogSymbolID>(candidate.entity)
                       ));
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

auto AnalysisCatalogBuilder::bind_cpp_imports(
    ProgramModuleID module_id,
    const std::flat_map<std::string, Span, std::less<>>& declarations
) noexcept -> void {
    const auto ast = draft.syntax_tree(module_id).view();
    const auto source_id = ast.source_id();
    const auto& ast_module = ast.ast_module();
    auto cpp_bindings = std::vector<CatalogCppBinding>();
    auto cpp_selections = std::flat_map<std::string, std::vector<std::size_t>, std::less<>>();
    for (const auto [header_index, header] : std::views::enumerate(ast_module.cpp_header_imports)) {
        if (!header.using_clause) {
            continue;
        }
        const auto& clause = *header.using_clause;
        auto prefix = std::vector<std::string>();
        const auto spelling = [&](Span span) noexcept {
            auto name = draft.source_slice_copy(module_id, span);
            if (!is_supported_cpp_identifier(name)) {
                diagnostics.push_back(DiagnosticBuilder(
                                          DiagnosticCode::CppIdentifier,
                                          "external name cannot be represented as a C++ identifier"
                )
                                          .primary(locate(source_id, span))
                                          .build());
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
                if (declarations.contains(components.back())) {
                    diagnostics.push_back(binding_error(
                        source_id,
                        "a C++ import conflicts with a Carven declaration",
                        leaf
                    ));
                }
                auto& selections = cpp_selections[components.back()];
                if (!selections.empty()
                    && cpp_bindings[selections.front()].components != components) {
                    diagnostics.push_back(binding_error(
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
        clause.selection.visit(
            Overloaded {
                [&](const ASTCppSingleSelection& value) noexcept { append(value.name, false); },
                [&](const ASTCppListSelection& value) noexcept {
                    for (const auto name : value.names) {
                        append(name, false);
                    }
                },
                [&](const ASTCppNamespaceSelection& value) noexcept { append(value.star, true); },
            }
        );
    }
    result.cpp_bindings.push_back(std::move(cpp_bindings));
    result.cpp_selections.push_back(std::move(cpp_selections));
}

auto AnalysisCatalogBuilder::bind_imports() noexcept -> void {
    const auto syntax_trees = draft.syntax_trees();
    const auto module_count = draft.module_count();
    for (auto module_index = 0uz; module_index < module_count; ++module_index) {
        const auto module_id = draft.provenance_module_at(module_index);
        if (draft.imported(module_id)) {
            continue;
        }
        const auto source_id = syntax_trees[module_index]->view().source_id();
        const auto source_module_path = draft.module_path_copy(module_id);
        const auto ast = syntax_trees[module_index]->view();
        const auto& ast_module = ast.ast_module();
        const auto& local_module = result.modules[module_index];
        auto& candidates = result.visible_candidates[module_index];

        struct ExplicitSelection final {
            CatalogEntity entity;
            Span origin;
        };

        auto explicit_names = std::flat_map<std::string, ExplicitSelection, std::less<>>();
        auto wildcard_targets = std::map<ResolvedModuleTarget, ImportBindingID>();

        const auto closed_imports = draft.resolved_imports(module_id);
        if (closed_imports.size() != ast_module.module_imports.size()) {
            invariant_violation("resolved import row is not aligned with source imports");
        }
        for (const auto& closed_import : closed_imports) {
            const auto import_id = closed_import.declaration;
            const auto& module_import = ast.module_import(import_id);
            const auto& reference = module_import.module_reference;
            const auto target_id = closed_import.target;
            auto target_entities = std::vector<CatalogEntity>();
            if (target_id.source) {
                for (const auto id : result.modules[target_id.source->index()].symbols) {
                    target_entities.emplace_back(id);
                }
            }
            if (target_id.provided) {
                for (const auto& entry : provided_exports()) {
                    if (entry.module == *target_id.provided && !entry.module_local) {
                        target_entities.push_back(entry.identity);
                    }
                }
            }
            const auto entity_name = [&](CatalogEntity entity) noexcept -> std::string_view {
                if (const auto* symbol = result.view().symbol(entity)) {
                    return symbol->name;
                }
                return provided_export(std::get<ProvidedExport>(entity)).name;
            };
            const auto entity_visible = [&](CatalogEntity entity) noexcept {
                const auto* symbol = result.view().symbol(entity);
                return !symbol
                    || visible_to(draft, symbol->visibility, symbol->module_id, module_id);
            };
            const auto target_path = target_id.source
                ? std::string(draft.module_path_copy(*target_id.source).value())
                : std::string(provided_module_path(*target_id.provided));

            auto selected_names = std::vector<ASTImportItem>();
            auto selection_kind = CatalogImportSelectionKind::Single;
            module_import.selection.value.visit(
                [&]<typename Selection>(const Selection& selection) noexcept {
                    if constexpr (std::same_as<Selection, ASTSingleImport>) {
                        selected_names.push_back(selection.item);
                    } else if constexpr (std::same_as<Selection, ASTImportList>) {
                        selection_kind = CatalogImportSelectionKind::List;
                        selected_names.assign(selection.names.begin(), selection.names.end());
                    } else if constexpr (std::same_as<Selection, ASTWildcardImport>) {
                        selection_kind = CatalogImportSelectionKind::Wildcard;
                    } else {
                        static_assert(std::same_as<Selection, void>, "unhandled import selection");
                    }
                }
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
                .selections = {},
            });

            auto names_in_declaration = std::flat_map<std::string, Span, std::less<>>();

            for (const auto& item : selected_names) {
                const auto name_span = item.alias_span.value_or(item.name_span);
                const auto name = draft.source_slice_copy(module_id, name_span);
                const auto export_name = draft.source_slice_copy(module_id, item.name_span);
                if (name == "ptr") {
                    diagnostics.push_back(import_error(
                        source_id,
                        "'ptr' is reserved for pointer type syntax",
                        name_span
                    ));
                    continue;
                }
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
                const auto found =
                    std::ranges::find_if(target_entities, [&](CatalogEntity entity) noexcept {
                        return entity_name(entity) == export_name;
                    });
                if (found == target_entities.end()) {
                    diagnostics.push_back(import_error(
                        source_id,
                        std::format(
                            "module '{}' has no declaration named '{}'",
                            target_path,
                            export_name
                        ),
                        item.name_span
                    ));
                    continue;
                }
                const auto entity = *found;
                if (!entity_visible(entity)) {
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
                    if (prior->second.entity == entity) {
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
                    ExplicitSelection {.entity = entity, .origin = name_span}
                );
                result.import_bindings[binding_id.index()].selections.push_back({
                    .entity = entity,
                    .name = std::string(name),
                    .origin = name_span,
                    .export_origin = item.name_span,
                    .aliased = item.alias_span.has_value(),
                });
                remove_wildcard_candidates(result, candidates, name);
                candidates[std::string(name)].push_back(
                    {.entity = entity, .import_binding = binding_id}
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
                    std::format("module '{}' is imported by wildcard more than once", target_path)
                ));
                continue;
            }
            wildcard_targets.emplace(target_id, binding_id);
            for (const auto entity : target_entities) {
                const auto name = entity_name(entity);
                if (!entity_visible(entity)) {
                    continue;
                }
                result.import_bindings[binding_id.index()].selections.push_back({
                    .entity = entity,
                    .name = std::string(name),
                    .origin = module_import.selection.span,
                    .export_origin = module_import.selection.span,
                    .aliased = false,
                });
                if (contains_local_declaration(candidates, local_module, name)
                    || has_explicit_candidate(result, candidates, name)) {
                    continue;
                }
                candidates[std::string(name)].push_back(
                    {.entity = entity, .import_binding = binding_id}
                );
            }
        }
    }

    for (auto module_index = 0uz; module_index < module_count; ++module_index) {
        const auto module_id = draft.provenance_module_at(module_index);
        if (draft.imported(module_id)) {
            continue;
        }
        for (const auto& binding : result.cpp_bindings[module_index]) {
            if (!binding.opens_namespace
                && result.visible_candidates[module_index].contains(binding.components.back())) {
                diagnostics.push_back(binding_error(
                    draft.module_source_id(module_id),
                    "a C++ import conflicts with a Carven binding",
                    binding.origin
                ));
            }
        }
    }
}

auto AnalysisCatalogBuilder::provide_defaults() noexcept -> void {
    for (auto index = 0uz; index < draft.module_count(); ++index) {
        auto& candidates = result.visible_candidates[index];
        for (const auto& info : provided_exports()) {
            if (info.default_available
                && !candidates.contains(info.name)
                && !result.cpp_selections[index].contains(info.name)) {
                candidates[std::string(info.name)].push_back(
                    {.entity = info.identity, .import_binding = std::nullopt}
                );
            }
        }
        for (const auto& info : standard_declarations()) {
            if (!info.default_available
                || candidates.contains(info.name)
                || result.cpp_selections[index].contains(info.name)) {
                continue;
            }
            if (const auto* symbol = result.view().standard_declaration(draft, info.identity)) {
                candidates[std::string(info.name)].push_back(
                    {.entity = symbol->symbol_id, .import_binding = std::nullopt}
                );
            }
        }
    }
}
