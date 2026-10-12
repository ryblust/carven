module carven:semantic.analysis.names.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.suggestion;
import :semantic.analysis.names;
import :semantic.analysis.program;
import :source.cpp.identifier;
import :support.invariant;
import std;

auto check_class_representation_access(
    const ProgramDraft& draft,
    ProgramModuleID source_module,
    StructID owner,
    std::optional<StructID> lexical_class,
    Span span
) noexcept -> AnalysisResult<void> {
    if (draft.construction_struct_declaration_copy(owner).kind == RecordKind::Class
        && lexical_class != owner) {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::AccessClassPrivate,
                "class representation is accessible only inside its defining class"
            )
                .primary(locate(draft.syntax_tree(source_module).view().source_id(), span))
                .build()
        ));
    }
    return {};
}

auto lookup_cpp_name(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID source_module,
    CppNameLookup lookup,
    std::span<const Span> components
) noexcept -> AnalysisResult<std::optional<CppNameReference>> {
    if (components.empty()) {
        invariant_violation("C++ lookup requires a name");
    }
    const auto root = draft.source_slice_copy(source_module, components.front());
    auto selected = std::optional<std::vector<std::string>>();
    if (lookup == CppNameLookup::ModuleScope) {
        const auto imports = catalog.cpp_imports(source_module);
        const auto selections = catalog.cpp_selection(source_module, root);
        if (!selections.empty()) {
            selected = imports[selections.front()].components;
            for (const auto index : selections) {
                usage.record_cpp(source_module, imports[index].origin);
            }
        } else if (!std::ranges::any_of(
                       imports,
                       [](const CatalogCppBinding& binding) static noexcept {
                           return binding.opens_namespace;
                       }
                   )) {
            return std::nullopt;
        }
    }
    auto names = std::vector<std::string>();
    for (const auto component : components) {
        auto name = draft.source_slice_copy(source_module, component);
        if (!is_supported_cpp_identifier(name)) {
            return std::unexpected(draft.diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::CppIdentifier,
                    "external name cannot be represented as a C++ identifier"
                )
                    .primary(locate(draft.syntax_tree(source_module).view().source_id(), component))
                    .build()
            ));
        }
        names.push_back(std::move(name));
    }
    if (selected) {
        selected->insert(selected->end(), names.begin() + 1, names.end());
        names = std::move(*selected);
        lookup = CppNameLookup::Global;
    }
    return CppNameReference {
        .context_module = catalog.find_module(source_module)->declaration,
        .lookup = lookup,
        .components = std::move(names)
    };
}

auto lookup_class_operation(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ProgramModuleID source_module,
    StructID owner,
    std::optional<StructID> lexical_class,
    std::string_view name,
    bool receiver,
    Span span
) noexcept -> AnalysisResult<FunctionID> {
    const auto fail = [&](DiagnosticCode code, std::string message) noexcept {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(code, std::move(message))
                .primary(locate(draft.syntax_tree(source_module).view().source_id(), span))
                .build()
        ));
    };
    if (owner.index() >= catalog.struct_count()) {
        return fail(DiagnosticCode::TypeMethodCall, "class has no operation with this name");
    }
    const auto* symbol = catalog.symbol(catalog.struct_symbol(owner));
    const auto& form = std::get<CatalogStructForm>(symbol->form);
    for (const auto function : form.operations) {
        const auto* selected = catalog.symbol(catalog.function_symbol(function));
        if (selected->name != name) {
            continue;
        }
        const auto operation = *selected->class_operation;
        if (operation.visibility == MemberVisibility::Private && lexical_class != owner) {
            return fail(
                DiagnosticCode::AccessClassPrivate,
                "private operation is accessible only inside its defining class"
            );
        }
        if (operation.receiver != receiver) {
            return fail(
                DiagnosticCode::TypeMethodCall,
                operation.receiver ? "instance operation requires a receiver"
                                   : "associated operation requires Type::name"
            );
        }
        return function;
    }
    return fail(
        DiagnosticCode::TypeMemberUnresolved,
        std::format("class has no operation named '{}'", name)
    );
}

auto select_catalog_entity(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID module,
    std::string_view name,
    Span origin,
    SourceObservation* observations,
    DiagnosticCode unresolved
) noexcept -> AnalysisResult<CatalogEntity> {
    const auto location = locate(draft.syntax_tree(module).view().source_id(), origin);
    const auto candidates = catalog.lookup(module, name);
    if (candidates.empty()) {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(
                unresolved,
                std::format(
                    "unresolved {}name '{}'{}",
                    unresolved == DiagnosticCode::TypeUnresolved ? "type " : "",
                    name,
                    spelling_suggestion(name, catalog.visible_names(module))
                )
            )
                .primary(location)
                .build()
        ));
    }
    if (candidates.size() != 1uz) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::NameAmbiguous,
            std::format("name '{}' is provided by more than one wildcard import", name)
        );
        diagnostic.primary(location, "ambiguous reference");
        for (const auto& candidate : candidates) {
            if (candidate.import_binding) {
                const auto& binding = catalog.imports()[candidate.import_binding->index()];
                diagnostic.related(
                    locate(location.source_id, binding.reference_span),
                    "candidate import"
                );
            }
        }
        return std::unexpected(draft.diagnostics().error(diagnostic.build()));
    }
    const auto& candidate = candidates.front();
    const auto entity = catalog.source_entity(draft, candidate.entity);
    auto alias = std::optional<SourceSpan>();
    if (candidate.import_binding) {
        usage.record(*candidate.import_binding);
        const auto& binding = catalog.imports()[candidate.import_binding->index()];
        for (const auto& selected : binding.selections) {
            if (selected.name == name && selected.aliased) {
                alias = locate(location.source_id, selected.origin);
                break;
            }
        }
    }
    if (observations) {
        observations->record(draft, location, entity, std::nullopt, alias);
    }
    return candidate.entity;
}
