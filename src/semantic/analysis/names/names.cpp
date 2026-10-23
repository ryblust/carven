module carven:semantic.analysis.names.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.suggestion;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.tree;
import :semantic.analysis.names;
import :semantic.analysis.program;
import :semantic.semir.generic;
import :source.cpp.identifier;
import :support.invariant;
import std;

auto source_name_available(
    const ProgramDraft& draft,
    ProgramModuleID module,
    std::string_view name
) noexcept -> bool {
    if (draft.imported(module)) {
        return true;
    }
    const auto& scope = draft.syntax_tree(module).view().ast_module();
    return scope.scope_complete
        && std::ranges::none_of(scope.unavailable_names, [&](Span unavailable) noexcept {
               return draft.source_slice_copy(module, unavailable) == name;
           });
}

auto class_declaration(const ProgramDraft& draft, ConstructionStructRef owner) noexcept
    -> ClassDeclarationRef {
    if (const auto* concrete = std::get_if<StructID>(&owner)) {
        if (const auto instance = draft.generic_nominal_instance_copy(*concrete)) {
            return instance->definition;
        }
        return *concrete;
    }
    return std::get<GenericNominalApplication>(
               draft.generic_type_copy(std::get<GenericTypeID>(owner))
    )
        .definition;
}

auto source_name_path(const ASTNamedType& type) noexcept -> SourceNamePath {
    return {
        .global_root = type.global_root,
        .components = type.components | std::views::transform(&ASTTypeNameComponent::name_span)
            | std::ranges::to<std::vector>()
    };
}

auto source_name_path(ASTView syntax, ASTExprID expression) noexcept
    -> std::optional<SourceNamePath> {
    auto path = SourceNamePath {.global_root = std::nullopt, .components = {}};
    while (true) {
        const auto& value = syntax.expression(expression).value;
        if (const auto* group = std::get_if<ASTGroupExpr>(&value)) {
            expression = group->expression;
        } else if (const auto* member = std::get_if<ASTMemberExpr>(&value);
                   member && member->op == ASTMemberOperator::Scope) {
            path.components.push_back(member->name_span);
            expression = member->operand_id;
        } else if (const auto* name = std::get_if<ASTNameExpr>(&value)) {
            path.components.push_back(name->name_span);
            break;
        } else if (const auto* native = std::get_if<ASTCppNameExpr>(&value)) {
            path.global_root = native->global_root;
            path.components.insert(
                path.components.end(),
                native->components.rbegin(),
                native->components.rend()
            );
            break;
        } else {
            return std::nullopt;
        }
    }
    std::ranges::reverse(path.components);
    return path;
}

auto class_representation_accessible(
    const ProgramDraft& draft,
    ConstructionStructRef owner,
    std::optional<ClassDeclarationRef> lexical_class
) noexcept -> bool {
    const auto declaration = class_declaration(draft, owner);
    const auto kind = std::holds_alternative<StructID>(declaration)
        ? draft.construction_struct_declaration_copy(std::get<StructID>(declaration)).kind
        : std::get<GenericRecordDefinition>(
              draft.generic_declaration_copy(std::get<GenericDeclarationID>(declaration))
          )
              .kind;
    return kind != RecordKind::Class || lexical_class == declaration;
}

auto check_class_representation_access(
    const ProgramDraft& draft,
    ProgramModuleID source_module,
    ConstructionStructRef owner,
    std::optional<ClassDeclarationRef> lexical_class,
    Span span
) noexcept -> AnalysisResult<void> {
    if (!class_representation_accessible(draft, owner, lexical_class)) {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::AccessClassPrivate,
                "class representation is accessible only inside its defining class"
            )
                .primary(locate(draft.module_source_id(source_module), span))
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
    const SourceNamePath& path,
    SourceObservation* observations
) noexcept -> AnalysisResult<std::optional<CppNameReference>> {
    const auto& components = path.components;
    if (components.empty()) {
        invariant_violation("C++ lookup requires a name");
    }
    const auto root = draft.source_slice_copy(source_module, components.front());
    if (!path.global_root && !source_name_available(draft, source_module, root)) {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(DiagnosticCode::ImportResolution, "module binding is unavailable")
                .primary(locate(draft.module_source_id(source_module), components.front()))
                .build()
        ));
    }
    auto selected = std::optional<std::vector<std::string>>();
    auto lookup = path.global_root ? CppNameLookup::Global : CppNameLookup::ModuleScope;
    auto binding = std::optional<SourceSpan>();
    if (lookup == CppNameLookup::ModuleScope) {
        // Carven declarations take precedence in both type and value positions.
        if (!catalog.lookup(source_module, root).empty()) {
            return std::nullopt;
        }
        const auto imports = catalog.cpp_imports(source_module);
        const auto selections = catalog.cpp_selection(source_module, root);
        if (!selections.empty()) {
            selected = imports[selections.front()].components;
            binding =
                locate(draft.module_source_id(source_module), imports[selections.front()].origin);
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
    auto names = selected.value_or(std::vector<std::string>());
    for (const auto component : components | std::views::drop(selected ? 1uz : 0uz)) {
        auto name = draft.source_slice_copy(source_module, component);
        if (!is_supported_cpp_identifier(name)) {
            return std::unexpected(draft.diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::CppIdentifier,
                    "external name cannot be represented as a C++ identifier"
                )
                    .primary(locate(draft.module_source_id(source_module), component))
                    .build()
            ));
        }
        names.push_back(std::move(name));
    }
    if (selected) {
        lookup = CppNameLookup::Global;
    }
    if (observations) {
        auto path = lookup == CppNameLookup::Global ? std::string("::") : std::string();
        for (const auto& name : names) {
            if (!path.empty() && !path.ends_with("::")) {
                path += "::";
            }
            path += name;
        }
        auto documentation = std::string(
            lookup == CppNameLookup::Global
                ? "External C++ name. Signature, overloads and availability are checked by the C++ compiler."
                : "Unqualified C++ lookup. The C++ compiler resolves the name in the imported namespace environment."
        );
        const auto bindings = catalog.cpp_imports(source_module);
        auto headers = std::set<std::size_t>();
        for (const auto& binding : bindings) {
            if ((selected
                 && !binding.opens_namespace
                 && draft.source_slice_copy(source_module, binding.origin) == root)
                || (lookup == CppNameLookup::ModuleScope && binding.opens_namespace)) {
                headers.insert(binding.header_index);
            }
        }
        if (!headers.empty()) {
            const auto syntax = draft.syntax_tree(source_module).view();
            for (const auto index : headers) {
                const auto& header = syntax.ast_module().cpp_header_imports[index];
                documentation += "\nImport: " + draft.source_slice_copy(source_module, header.span);
            }
        }
        if (binding && components.size() > 1uz) {
            // Only the selected root is a Carven binding. Appended C++ members
            // have no known declaration, even though their path is retained.
            auto root_path = std::string();
            const auto selections = catalog.cpp_selection(source_module, root);
            for (const auto& component : bindings[selections.front()].components) {
                root_path += "::" + component;
            }
            observations->external(
                locate(draft.module_source_id(source_module), components.front()),
                std::move(root_path),
                documentation,
                binding
            );
        }
        observations->external(
            locate(draft.module_source_id(source_module), components.back()),
            std::move(path),
            std::move(documentation),
            components.size() == 1uz ? binding : std::nullopt
        );
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
    ConstructionStructRef owner,
    std::optional<ClassDeclarationRef> lexical_class,
    std::string_view name,
    bool receiver,
    Span span
) noexcept -> AnalysisResult<CatalogSymbolID> {
    const auto fail = [&](DiagnosticCode code, std::string message) noexcept {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(code, std::move(message))
                .primary(locate(draft.module_source_id(source_module), span))
                .build()
        ));
    };
    const auto declaration = class_declaration(draft, owner);
    const auto* symbol = std::holds_alternative<StructID>(declaration)
        ? catalog.symbol(catalog.struct_symbol(std::get<StructID>(declaration)))
        : catalog.symbol(catalog.generic_symbol(std::get<GenericDeclarationID>(declaration)));
    const auto& operations = std::holds_alternative<CatalogStructForm>(symbol->form)
        ? std::get<CatalogStructForm>(symbol->form).operations
        : std::get<CatalogGenericForm>(symbol->form).operations;
    for (const auto operation_id : operations) {
        const auto* selected = catalog.symbol(operation_id);
        if (selected->name != name) {
            continue;
        }
        const auto operation = *selected->class_operation;
        if (operation.visibility == MemberVisibility::Private && lexical_class != operation.owner) {
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
        return operation_id;
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
    ProgramModuleID module_id,
    std::string_view name,
    Span origin,
    SourceObservation* observations,
    DiagnosticCode unresolved
) noexcept -> AnalysisResult<CatalogEntity> {
    const auto location = locate(draft.module_source_id(module_id), origin);
    if (!source_name_available(draft, module_id, name)) {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(DiagnosticCode::ImportResolution, "module binding is unavailable")
                .primary(location)
                .build()
        ));
    }
    const auto candidates = catalog.lookup(module_id, name);
    if (candidates.empty()) {
        const auto suggested = closest_spelling(name, catalog.visible_names(module_id));
        auto diagnostic = DiagnosticBuilder(
            unresolved,
            std::format(
                "unresolved {}name '{}'{}",
                unresolved == DiagnosticCode::TypeUnresolved ? "type " : "",
                name,
                suggested ? std::format("; did you mean '{}'?", *suggested) : std::string()
            )
        );
        diagnostic.primary(location);
        if (suggested) {
            diagnostic.fix(std::format("Change to '{}'", *suggested), {{location, *suggested}});
        }
        return std::unexpected(draft.diagnostics().error(diagnostic.build()));
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
    if (const auto* declaration = catalog.symbol(candidate.entity);
        declaration && !source_name_available(draft, declaration->module_id, declaration->name)) {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(DiagnosticCode::ImportResolution, "module binding is unavailable")
                .primary(location)
                .build()
        ));
    }
    const auto entity = catalog.source_entity(draft, candidate.entity);
    auto alias = std::optional<SourceSpan>();
    if (candidate.import_binding) {
        usage.record(*candidate.import_binding, name);
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
