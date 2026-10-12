module carven:semantic.analysis.types.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.names;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.source.builder;
import :semantic.analysis.types;
import :support.invariant;
import :support.visit;
import std;

auto provided_builtin_type(ProvidedExport identity) noexcept -> std::optional<BuiltinType> {
    switch (identity) {
        case ProvidedExport::Bool:   return BuiltinType::Bool;
        case ProvidedExport::Char:   return BuiltinType::Char;
        case ProvidedExport::String: return BuiltinType::String;
        case ProvidedExport::Str:    return BuiltinType::Str;
        case ProvidedExport::I8:     return BuiltinType::I8;
        case ProvidedExport::I16:    return BuiltinType::I16;
        case ProvidedExport::I32:    return BuiltinType::I32;
        case ProvidedExport::I64:    return BuiltinType::I64;
        case ProvidedExport::U8:     return BuiltinType::U8;
        case ProvidedExport::U16:    return BuiltinType::U16;
        case ProvidedExport::U32:    return BuiltinType::U32;
        case ProvidedExport::U64:    return BuiltinType::U64;
        case ProvidedExport::Isize:  return BuiltinType::Isize;
        case ProvidedExport::Usize:  return BuiltinType::Usize;
        case ProvidedExport::F32:    return BuiltinType::F32;
        case ProvidedExport::F64:    return BuiltinType::F64;
        case ProvidedExport::Void:   return BuiltinType::Void;
        case ProvidedExport::U8x32:  return BuiltinType::U8x32;
        case ProvidedExport::Mask32: return BuiltinType::Mask32;
        case ProvidedExport::F32x8:  return BuiltinType::F32x8;
        case ProvidedExport::Mask8:  return BuiltinType::Mask8;
        case ProvidedExport::U8x16:  return BuiltinType::U8x16;
        case ProvidedExport::Mask16: return BuiltinType::Mask16;
        case ProvidedExport::F32x4:  return BuiltinType::F32x4;
        case ProvidedExport::Mask4:  return BuiltinType::Mask4;
        default:                     return std::nullopt;
    }
}

auto source_type_name_is_reserved(std::string_view name) noexcept -> bool {
    return name == "ptr";
}

namespace {

auto source_id(const ProgramDraft& draft, ProgramModuleID module_id) noexcept -> SourceID {
    return draft.syntax_tree(module_id).view().source_id();
}

auto fail(
    const ProgramDraft& draft,
    ProgramModuleID module_id,
    Span span,
    DiagnosticCode code,
    std::string message
) noexcept -> AnalysisFailure {
    return draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                         .primary(locate(source_id(draft, module_id), span))
                                         .build());
}

auto require_type_argument(
    const ProgramDraft& draft,
    ProgramModuleID module_id,
    const ASTGenericArgument& argument
) noexcept -> AnalysisResult<ASTTypeID> {
    const auto* type = std::get_if<ASTTypeID>(&argument.value);
    if (!type) {
        return std::unexpected(fail(
            draft,
            module_id,
            argument.span,
            DiagnosticCode::TypeGenericArguments,
            "this constructor requires one type argument rather than a failure set"
        ));
    }
    return *type;
}

auto select_global_entity(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    std::string_view name,
    Span origin,
    SourceObservation* observations
) noexcept -> AnalysisResult<CatalogEntity> {
    return select_catalog_entity(
        draft,
        catalog,
        import_usage,
        module_id,
        name,
        origin,
        observations,
        DiagnosticCode::TypeUnresolved
    );
}

auto resolve_named_value(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    const ASTNamedType& named,
    ASTView syntax,
    ArrayExtentResolver resolve_extent,
    Span origin,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    const auto root = draft.source_slice_copy(module_id, named.components.front().name_span);
    if (!named.global_root && generic_parameter_index(draft, generic_context, root)) {
        co_return std::unexpected(fail(
            draft,
            module_id,
            origin,
            DiagnosticCode::TypeGenericDefinition,
            "this type position requires a concrete type rather than a type parameter"
        ));
    }
    auto selected_entity = std::optional<CatalogEntity>();
    if (!named.global_root && !catalog.lookup(module_id, root).empty()) {
        auto selected = select_global_entity(
            draft,
            catalog,
            import_usage,
            module_id,
            root,
            named.components.front().name_span,
            observations
        );
        if (!selected) {
            co_return std::unexpected(selected.error());
        }
        selected_entity = *selected;
    }
    const auto* provided =
        selected_entity ? std::get_if<ProvidedExport>(&*selected_entity) : nullptr;
    if (provided && named.components.size() == 1uz && *provided == ProvidedExport::Operation) {
        if (named.arguments.empty() || named.arguments.size() > 2uz) {
            co_return std::unexpected(fail(
                draft,
                module_id,
                origin,
                DiagnosticCode::TypeGenericArguments,
                "Operation requires a result type and an optional throw failure set"
            ));
        }
        const auto result_argument =
            require_type_argument(draft, module_id, named.arguments.front());
        if (!result_argument) {
            co_return std::unexpected(result_argument.error());
        }
        auto success = co_await resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            *result_argument,
            resolve_extent,
            requests,
            generic_context,
            observations
        );
        if (!success) {
            co_return std::unexpected(success.error());
        }
        auto checked = require_source_value_type(
            draft,
            *success,
            module_id,
            origin,
            "Operation successful result"
        );
        if (!checked) {
            co_return std::unexpected(checked.error());
        }
        auto failures = ConstructionFailureRef(draft.empty_failure_set());
        if (named.arguments.size() == 2uz) {
            const auto& argument = named.arguments[1];
            const auto* clause = std::get_if<ASTThrowClause>(&argument.value);
            if (!clause) {
                co_return std::unexpected(fail(
                    draft,
                    module_id,
                    argument.span,
                    DiagnosticCode::TypeGenericArguments,
                    "Operation's second argument must be a throw failure set"
                ));
            }
            auto resolved = co_await resolve_failure_types(
                draft,
                catalog,
                import_usage,
                module_id,
                syntax,
                *clause,
                resolve_extent,
                requests,
                generic_context,
                observations
            );
            if (!resolved) {
                co_return std::unexpected(resolved.error());
            }
            failures = std::move(*resolved);
        }
        co_return make_operation_type(draft, *success, failures);
    }
    if (provided && named.components.size() == 1uz && *provided == ProvidedExport::Vector) {
        if (named.arguments.size() != 1uz) {
            co_return std::unexpected(fail(
                draft,
                module_id,
                origin,
                DiagnosticCode::TypeUnresolved,
                "Vector requires one element type"
            ));
        }
        const auto selected_type = require_type_argument(draft, module_id, named.arguments.front());
        if (!selected_type) {
            co_return std::unexpected(selected_type.error());
        }
        auto element = (co_await resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            *selected_type,
            resolve_extent,
            requests,
            generic_context,
            observations
        ));
        if (!element) {
            co_return std::unexpected(element.error());
        }
        if (auto checked =
                require_source_value_type(draft, *element, module_id, origin, "Vector element");
            !checked) {
            co_return std::unexpected(checked.error());
        }
        const auto concrete = draft.canonicalize_declared_type(*element);
        draft.record_vector_element(
            concrete,
            draft.append_source_origin(draft.module_source(module_id), origin)
        );
        co_return ConstructionTypeRef {
            draft.intern_type({.value = VectorTypeValue {.element = concrete}})
        };
    }
    if (provided && named.components.size() == 1uz && *provided == ProvidedExport::Range) {
        if (named.arguments.size() != 1uz) {
            co_return std::unexpected(fail(
                draft,
                module_id,
                origin,
                DiagnosticCode::TypeUnresolved,
                "Range requires one integer element type"
            ));
        }
        const auto selected_type = require_type_argument(draft, module_id, named.arguments.front());
        if (!selected_type) {
            co_return std::unexpected(selected_type.error());
        }
        auto element = (co_await resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            *selected_type,
            resolve_extent,
            requests,
            generic_context,
            observations
        ));
        if (!element) {
            co_return std::unexpected(element.error());
        }
        const auto* concrete = std::get_if<TypeID>(&*element);
        if (concrete != nullptr) {
            const auto type = draft.type_copy(*concrete);
            const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
            if (builtin != nullptr && builtin_is_integer(builtin->kind)) {
                co_return ConstructionTypeRef {
                    draft.intern_type({.value = RangeTypeValue {.element = *concrete}})
                };
            }
        }
        co_return std::unexpected(fail(
            draft,
            module_id,
            origin,
            DiagnosticCode::TypeRangeInteger,
            "range element must be an integer type"
        ));
    }
    if (named.global_root.has_value() || !selected_entity) {
        auto components = std::vector<Span>();
        for (const auto& component : named.components) {
            components.push_back(component.name_span);
        }
        auto name = lookup_cpp_name(
            draft,
            catalog,
            import_usage,
            module_id,
            named.global_root.has_value() ? CppNameLookup::Global : CppNameLookup::ModuleScope,
            components
        );
        if (!name.has_value()) {
            co_return std::unexpected(name.error());
        }
        if (name->has_value()) {
            auto arguments = std::vector<TypeID>();
            for (const auto& argument : named.arguments) {
                const auto selected_type = require_type_argument(draft, module_id, argument);
                if (!selected_type) {
                    co_return std::unexpected(selected_type.error());
                }
                auto resolved = (co_await resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    *selected_type,
                    resolve_extent,
                    requests,
                    generic_context,
                    observations
                ));
                if (!resolved.has_value()) {
                    co_return std::unexpected(resolved.error());
                }
                const auto* concrete = std::get_if<TypeID>(&*resolved);
                if (concrete == nullptr) {
                    co_return std::unexpected(fail(
                        draft,
                        module_id,
                        origin,
                        DiagnosticCode::TypeUnresolved,
                        "C++ type arguments require concrete types"
                    ));
                }
                arguments.push_back(*concrete);
            }
            co_return ConstructionTypeRef {draft.intern_type(
                {.value = CppTypeValue {
                     .form =
                         CppNamedType {.name = std::move(**name), .arguments = std::move(arguments)}
                 }}
            )};
        }
    }

    if (named.components.size() != 1uz) {
        co_return std::unexpected(fail(
            draft,
            module_id,
            origin,
            DiagnosticCode::TypeUnresolved,
            "type names cannot contain value-member qualification"
        ));
    }
    const auto component = named.components.front().name_span;
    const auto name = draft.source_slice_copy(module_id, component);
    if (const auto builtin = provided ? provided_builtin_type(*provided) : std::nullopt) {
        if (!named.arguments.empty()) {
            co_return std::unexpected(fail(
                draft,
                module_id,
                origin,
                DiagnosticCode::TypeGenericArguments,
                "builtin type does not accept type arguments"
            ));
        }
        co_return ConstructionTypeRef {draft.builtin_type(*builtin)};
    }
    const auto selected = selected_entity ? AnalysisResult<CatalogEntity>(*selected_entity)
                                          : select_global_entity(
                                                draft,
                                                catalog,
                                                import_usage,
                                                module_id,
                                                name,
                                                component,
                                                observations
                                            );
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    const auto* selected_symbol = catalog.symbol(*selected);
    if (const auto* generic =
            (selected_symbol ? std::get_if<CatalogGenericForm>(&selected_symbol->form) : nullptr)) {
        if (requests) {
            auto ready = (co_await requests
                              ->ensure_declaration(selected_symbol->symbol_id, module_id, origin));
            if (!ready) {
                co_return std::unexpected(ready.error());
            }
        }
        const auto contract = draft.generic_declaration_contract_copy(generic->definition);
        if (named.arguments.size() != contract.parameter_kinds.size()) {
            co_return std::unexpected(fail(
                draft,
                module_id,
                origin,
                DiagnosticCode::TypeGenericArguments,
                "generic argument count differs from its declaration"
            ));
        }
        auto resolved_arguments = co_await resolve_generic_source_arguments(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            named.arguments,
            contract.parameter_kinds,
            resolve_extent,
            requests,
            generic_context,
            observations
        );
        if (!resolved_arguments) {
            co_return std::unexpected(resolved_arguments.error());
        }
        auto arguments = CanonicalGenericEnvironment();
        for (const auto& argument : *resolved_arguments) {
            if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                arguments.emplace_back(draft.canonicalize_declared_type(*type));
            } else if (const auto* effect = std::get_if<ConstructionFailureRef>(&argument)) {
                const auto* closed = std::get_if<FailureSetID>(effect);
                if (!closed) {
                    co_return std::unexpected(fail(
                        draft,
                        module_id,
                        origin,
                        DiagnosticCode::TypeGenericArguments,
                        "a nominal instance requires a closed failure-set argument"
                    ));
                }
                arguments.emplace_back(*closed);
            }
        }
        auto instance = draft.instantiate_generic_nominal(
            generic->definition,
            arguments,
            draft.append_source_origin(draft.module_source(module_id), origin)
        );
        if (!instance) {
            co_return std::unexpected(instance.error());
        }
        co_return ConstructionTypeRef {*instance};
    }
    if (!named.arguments.empty()) {
        co_return std::unexpected(fail(
            draft,
            module_id,
            origin,
            DiagnosticCode::TypeGenericArguments,
            "type arguments require a type-parameterized declaration"
        ));
    }
    if (const auto* structure =
            (selected_symbol ? std::get_if<CatalogStructForm>(&selected_symbol->form) : nullptr)) {
        co_return ConstructionTypeRef {draft.intern_type(
            CanonicalType {
                .value = StructTypeValue {.structure = structure->structure},
            }
        )};
    }
    if (const auto* enumeration =
            (selected_symbol ? std::get_if<CatalogEnumForm>(&selected_symbol->form) : nullptr)) {
        co_return ConstructionTypeRef {draft.intern_type(
            CanonicalType {
                .value = EnumTypeValue {.enumeration = enumeration->enumeration},
            }
        )};
    }
    co_return std::unexpected(fail(
        draft,
        module_id,
        origin,
        DiagnosticCode::TypeUnresolved,
        std::format("'{}' does not name a type", name)
    ));
}

auto resolve_named(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    const ASTNamedType& named,
    ASTView syntax,
    ArrayExtentResolver resolve_extent,
    Span origin,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    auto result = co_await resolve_named_value(
        draft,
        catalog,
        import_usage,
        module_id,
        named,
        syntax,
        resolve_extent,
        origin,
        requests,
        generic_context,
        observations
    );
    if (result && observations) {
        observations->record(
            draft,
            locate(syntax.source_id(), named.components.back().name_span),
            std::nullopt,
            *result
        );
    }
    co_return result;
}

auto resolve_function_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTFunctionType& function,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    const auto concrete_callable_type = [&](ConstructionTypeRef type,
                                            Span at) noexcept -> AnalysisTask<ConstructionTypeRef> {
        const auto* generic = std::get_if<GenericTypeID>(&type);
        if (!generic) {
            co_return type;
        }
        auto pending = std::vector<ConstructionTypeRef> {*generic};
        auto visited = std::flat_set<ConstructionTypeRef>();
        auto dependencies = std::flat_set<GenericDeclarationID>();
        while (!pending.empty()) {
            const auto current = pending.back();
            pending.pop_back();
            if (!visited.insert(current).second) {
                continue;
            }
            const auto* pending_generic = std::get_if<GenericTypeID>(&current);
            if (!pending_generic) {
                continue;
            }
            const auto expression = draft.generic_type_copy(*pending_generic);
            if (std::holds_alternative<GenericTypeParameter>(expression)) {
                co_return std::unexpected(fail(
                    draft,
                    module_id,
                    at,
                    DiagnosticCode::TypeGenericDefinition,
                    "callable view signatures require concrete types"
                ));
            }
            if (const auto* application = std::get_if<GenericNominalApplication>(&expression)) {
                dependencies.insert(application->definition);
            }
            visit_generic_type_children(expression, [&](ConstructionTypeRef child) noexcept {
                pending.push_back(child);
            });
        }
        if (requests) {
            for (const auto definition : dependencies) {
                auto available = co_await requests->ensure_declaration(
                    catalog.generic_symbol(definition),
                    module_id,
                    at
                );
                if (!available) {
                    co_return std::unexpected(available.error());
                }
            }
        }
        auto resolved = draft.resolve_generic_type(
            *generic,
            generic_context->definition,
            {},
            draft.append_source_origin(draft.module_source(module_id), at)
        );
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        co_return ConstructionTypeRef(*resolved);
    };
    auto parameters = std::vector<ConstructionCallableParameter>();
    parameters.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
        auto type = (co_await resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            parameter.type,
            resolve_extent,
            requests,
            generic_context,
            observations
        ));
        if (!type.has_value()) {
            co_return std::unexpected(type.error());
        }
        auto concrete = co_await concrete_callable_type(*type, syntax.type(parameter.type).span);
        if (!concrete) {
            co_return std::unexpected(concrete.error());
        }
        auto value_type = require_source_value_type(
            draft,
            *concrete,
            module_id,
            syntax.type(parameter.type).span,
            "function parameter"
        );
        if (!value_type.has_value()) {
            co_return std::unexpected(value_type.error());
        }
        parameters.push_back({
            .stage = ParameterStage::Runtime,
            .access = semantic_access_mode(parameter.access),
            .type = *value_type,
        });
    }
    auto result = (co_await resolve_source_type(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        function.result_type,
        resolve_extent,
        requests,
        generic_context,
        observations
    ));
    if (!result.has_value()) {
        co_return std::unexpected(result.error());
    }
    auto concrete_result =
        co_await concrete_callable_type(*result, syntax.type(function.result_type).span);
    if (!concrete_result) {
        co_return std::unexpected(concrete_result.error());
    }
    auto failures = ConstructionFailureRef(draft.empty_failure_set());
    if (function.throw_clause.has_value()) {
        auto resolved = (co_await resolve_failure_types(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            *function.throw_clause,
            resolve_extent,
            requests,
            generic_context,
            observations
        ));
        if (!resolved.has_value()) {
            co_return std::unexpected(resolved.error());
        }
        failures = std::move(*resolved);
    }
    if (!std::holds_alternative<FailureSetID>(failures)) {
        co_return std::unexpected(fail(
            draft,
            module_id,
            function.throw_clause->span,
            DiagnosticCode::TypeGenericDefinition,
            "callable view signatures require a concrete failure set"
        ));
    }
    const auto bound = draft.append_construction_type(
        ConstructionType {
            .value = ConstructionCallableViewTypeValue {
                .parameters = std::move(parameters),
                .result = *concrete_result,
                .failures = failures,
            },
        }
    );
    co_return ConstructionTypeRef {draft.canonicalize_declared_type(bound)};
}

auto resolve_type_value(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTType& source_type,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    co_return (co_await source_type.value.visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept -> AnalysisTask<ConstructionTypeRef> {
                co_return (co_await resolve_named(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    named,
                    syntax,
                    resolve_extent,
                    source_type.span,
                    requests,
                    generic_context,
                    observations
                ));
            },
            [&](const ASTPointerType& pointer) noexcept -> AnalysisTask<ConstructionTypeRef> {
                auto target = (co_await resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    pointer.target,
                    resolve_extent,
                    requests,
                    generic_context,
                    observations
                ));
                if (!target) {
                    co_return std::unexpected(target.error());
                }
                if (pointer.access.mode == ASTAccessMode::Take) {
                    co_return std::unexpected(fail(
                        draft,
                        module_id,
                        source_type.span,
                        DiagnosticCode::TypeUnresolved,
                        "ptr target cannot have Take access"
                    ));
                }
                const auto access = pointer.access.mode == ASTAccessMode::Write
                    ? PointerAccess::Write
                    : PointerAccess::Read;
                co_return ConstructionTypeRef {draft.intern_type(
                    {.value = PointerTypeValue {
                         .target = draft.canonicalize_declared_type(*target),
                         .access = access
                     }}
                )};
            },
            [&](const ASTSliceType& view) noexcept -> AnalysisTask<ConstructionTypeRef> {
                auto element = (co_await resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    view.element_type,
                    resolve_extent,
                    requests,
                    generic_context,
                    observations
                ));
                if (!element) {
                    co_return std::unexpected(element.error());
                }
                auto checked = require_source_value_type(
                    draft,
                    *element,
                    module_id,
                    syntax.type(view.element_type).span,
                    "slice element"
                );
                if (!checked) {
                    co_return std::unexpected(checked.error());
                }
                if (const auto* concrete = std::get_if<TypeID>(&*checked)) {
                    co_return ConstructionTypeRef {
                        draft.intern_type({.value = SliceTypeValue {.element = *concrete}})
                    };
                }
                co_return ConstructionTypeRef {draft.append_construction_type(
                    {.value = ConstructionSliceTypeValue {.element = *checked}}
                )};
            },
            [&](const ASTArrayType& array) noexcept -> AnalysisTask<ConstructionTypeRef> {
                auto element = (co_await resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    array.element_type,
                    resolve_extent,
                    requests,
                    generic_context,
                    observations
                ));
                if (!element.has_value()) {
                    co_return std::unexpected(element.error());
                }
                auto value_element = require_source_value_type(
                    draft,
                    *element,
                    module_id,
                    syntax.type(array.element_type).span,
                    "array element"
                );
                if (!value_element.has_value()) {
                    co_return std::unexpected(value_element.error());
                }
                auto extent = co_await resolve_extent(array.extent);
                if (!extent.has_value()) {
                    co_return std::unexpected(extent.error());
                }
                if (const auto* concrete = std::get_if<TypeID>(&*value_element)) {
                    co_return ConstructionTypeRef {draft.intern_type(
                        CanonicalType {
                            .value = ArrayTypeValue {.element = *concrete, .extent = *extent},
                        }
                    )};
                }
                co_return ConstructionTypeRef {draft.append_construction_type(
                    ConstructionType {
                        .value = ConstructionArrayTypeValue {
                            .element = *value_element,
                            .extent = *extent,
                        },
                    }
                )};
            },
            [&](const ASTTupleType& tuple) noexcept -> AnalysisTask<ConstructionTypeRef> {
                auto elements = std::vector<ConstructionTypeRef>();
                auto concrete_elements = std::vector<TypeID>();
                elements.reserve(tuple.elements.size());
                concrete_elements.reserve(tuple.elements.size());
                for (const auto& element : tuple.elements) {
                    auto resolved = (co_await resolve_source_type(
                        draft,
                        catalog,
                        import_usage,
                        module_id,
                        syntax,
                        element,
                        resolve_extent,
                        requests,
                        generic_context,
                        observations
                    ));
                    if (!resolved) {
                        co_return std::unexpected(resolved.error());
                    }
                    auto checked = require_source_value_type(
                        draft,
                        *resolved,
                        module_id,
                        syntax.type(element).span,
                        "tuple element"
                    );
                    if (!checked) {
                        co_return std::unexpected(checked.error());
                    }
                    elements.push_back(*checked);
                    if (const auto* concrete = std::get_if<TypeID>(&*checked)) {
                        concrete_elements.push_back(*concrete);
                    }
                }
                if (concrete_elements.size() == elements.size()) {
                    co_return ConstructionTypeRef {draft.intern_type(
                        {.value = TupleTypeValue {.elements = std::move(concrete_elements)}}
                    )};
                }
                co_return ConstructionTypeRef {draft.append_construction_type(
                    {.value = ConstructionTupleTypeValue {.elements = std::move(elements)}}
                )};
            },
            [&](const ASTFunctionType& function) noexcept -> AnalysisTask<ConstructionTypeRef> {
                co_return (co_await resolve_function_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    function,
                    resolve_extent,
                    requests,
                    generic_context,
                    observations
                ));
            },
        }
    ));
}

} // namespace

auto semantic_access_mode(ASTAccessSyntax access) noexcept -> AccessMode {
    switch (access.mode) {
        case ASTAccessMode::Read:  return AccessMode::Read;
        case ASTAccessMode::Write: return AccessMode::Write;
        case ASTAccessMode::Take:  return AccessMode::Take;
    }
    std::unreachable();
}

namespace {

auto resolve_generic_type_contents(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTType& source,
    GenericTypeContext context,
    ArrayExtentResolver resolve_extent,
    bool value_required,
    ConstructionRequests* requests,
    SourceObservation* observations
) noexcept -> AnalysisTask<GenericTypeID> {
    co_return (co_await source.value.visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept -> AnalysisTask<GenericTypeID> {
                if (!named.global_root && named.components.size() == 1uz) {
                    const auto component = named.components.front().name_span;
                    const auto name = draft.source_slice_copy(module_id, component);
                    if (const auto index = generic_parameter_index(draft, &context, name)) {
                        if (*index < context.parameter_kinds.size()
                            && context.parameter_kinds[*index] != GenericParameterKind::Type) {
                            co_return std::unexpected(fail(
                                draft,
                                module_id,
                                source.span,
                                DiagnosticCode::TypeGenericDefinition,
                                "a failure-set parameter cannot be used as an object type"
                            ));
                        }
                        if (!named.arguments.empty()) {
                            co_return std::unexpected(fail(
                                draft,
                                module_id,
                                source.span,
                                DiagnosticCode::TypeGenericDefinition,
                                "a type parameter cannot be applied to type arguments"
                            ));
                        }

                        co_return draft.intern_generic_type(
                            GenericTypeParameter {.definition = context.definition, .index = *index}
                        );
                    }
                    auto selected_entity = std::optional<CatalogEntity>();
                    if (!catalog.lookup(module_id, name).empty()) {
                        const auto selected = select_global_entity(
                            draft,
                            catalog,
                            import_usage,
                            module_id,
                            name,
                            component,
                            observations
                        );
                        if (!selected) {
                            co_return std::unexpected(selected.error());
                        }
                        selected_entity = *selected;
                    }
                    const auto* provided =
                        selected_entity ? std::get_if<ProvidedExport>(&*selected_entity) : nullptr;
                    if (provided && *provided == ProvidedExport::Operation) {
                        if (named.arguments.empty() || named.arguments.size() > 2uz) {
                            co_return std::unexpected(fail(
                                draft,
                                module_id,
                                source.span,
                                DiagnosticCode::TypeGenericArguments,
                                "Operation requires a result type and an optional throw failure set"
                            ));
                        }
                        const auto result_argument =
                            require_type_argument(draft, module_id, named.arguments.front());
                        if (!result_argument) {
                            co_return std::unexpected(result_argument.error());
                        }
                        auto success = co_await resolve_generic_source_type(
                            draft,
                            catalog,
                            import_usage,
                            module_id,
                            syntax,
                            *result_argument,
                            context,
                            resolve_extent,
                            true,
                            requests,
                            observations
                        );
                        if (!success) {
                            co_return std::unexpected(success.error());
                        }
                        auto failures = ConstructionFailureRef(draft.empty_failure_set());
                        if (named.arguments.size() == 2uz) {
                            const auto& argument = named.arguments[1];
                            const auto* clause = std::get_if<ASTThrowClause>(&argument.value);
                            if (!clause) {
                                co_return std::unexpected(fail(
                                    draft,
                                    module_id,
                                    argument.span,
                                    DiagnosticCode::TypeGenericArguments,
                                    "Operation's second argument must be a throw failure set"
                                ));
                            }
                            auto resolved = co_await resolve_failure_types(
                                draft,
                                catalog,
                                import_usage,
                                module_id,
                                syntax,
                                *clause,
                                resolve_extent,
                                requests,
                                &context,
                                observations
                            );
                            if (!resolved) {
                                co_return std::unexpected(resolved.error());
                            }
                            failures = std::move(*resolved);
                        }
                        co_return draft.intern_generic_type(
                            GenericOperationType {
                                .success = *success,
                                .failures = failures,
                            }
                        );
                    }
                    if (provided && *provided == ProvidedExport::Vector) {
                        if (named.arguments.size() != 1uz) {
                            co_return std::unexpected(fail(
                                draft,
                                module_id,
                                source.span,
                                DiagnosticCode::TypeUnresolved,
                                "Vector requires one element type"
                            ));
                        }
                        const auto selected_type =
                            require_type_argument(draft, module_id, named.arguments.front());
                        if (!selected_type) {
                            co_return std::unexpected(selected_type.error());
                        }
                        auto element = (co_await resolve_generic_source_type(
                            draft,
                            catalog,
                            import_usage,
                            module_id,
                            syntax,
                            *selected_type,
                            context,
                            resolve_extent,
                            true,
                            requests,
                            observations
                        ));
                        if (!element) {
                            co_return std::unexpected(element.error());
                        }
                        co_return draft.intern_generic_type(
                            GenericVectorType {.element = *element}
                        );
                    }
                    if (const auto* selected_symbol =
                            selected_entity ? catalog.symbol(*selected_entity) : nullptr) {
                        if (const auto* generic =
                                std::get_if<CatalogGenericForm>(&selected_symbol->form)) {
                            // Symbolic applications bind argument domains without
                            // demanding recursive nominal fields. The completed
                            // declaration checker validates parameter kinds.
                            auto arguments = GenericEnvironment();
                            for (const auto& argument : named.arguments) {
                                auto resolved = co_await resolve_source_generic_argument(
                                    draft,
                                    catalog,
                                    import_usage,
                                    module_id,
                                    syntax,
                                    argument,
                                    resolve_extent,
                                    requests,
                                    &context,
                                    std::nullopt,
                                    observations
                                );
                                if (!resolved) {
                                    co_return std::unexpected(resolved.error());
                                }
                                arguments.push_back(std::move(*resolved));
                            }
                            co_return draft.intern_generic_type(
                                GenericNominalApplication {
                                    .definition = generic->definition,
                                    .arguments = std::move(arguments)
                                }
                            );
                        }
                    }
                }
                // Native type arguments remain concrete; a symbolic native provider
                // requires a separately checked boundary contract.
                auto concrete = (co_await resolve_type_value(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    source,
                    resolve_extent,
                    requests,
                    &context,
                    observations
                ));
                if (!concrete) {
                    co_return std::unexpected(concrete.error());
                }
                if (value_required) {
                    auto checked = require_source_value_type(
                        draft,
                        *concrete,
                        module_id,
                        source.span,
                        "generic stored value"
                    );
                    if (!checked) {
                        co_return std::unexpected(checked.error());
                    }
                }
                co_return draft.intern_generic_type(draft.canonicalize_declared_type(*concrete));
            },
            [&](const ASTArrayType& array) noexcept -> AnalysisTask<GenericTypeID> {
                auto element = (co_await resolve_generic_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    array.element_type,
                    context,
                    resolve_extent,
                    true,
                    requests,
                    observations
                ));
                if (!element) {
                    co_return std::unexpected(element.error());
                }
                auto extent = (co_await resolve_extent(array.extent));
                if (!extent) {
                    co_return std::unexpected(extent.error());
                }
                co_return draft.intern_generic_type(
                    GenericArrayType {.element = *element, .extent = *extent}
                );
            },
            [&](const ASTTupleType& tuple) noexcept -> AnalysisTask<GenericTypeID> {
                auto result = GenericTupleType {.elements = {}};
                for (const auto& element : tuple.elements) {
                    auto resolved = (co_await resolve_generic_source_type(
                        draft,
                        catalog,
                        import_usage,
                        module_id,
                        syntax,
                        element,
                        context,
                        resolve_extent,
                        true,
                        requests,
                        observations
                    ));
                    if (!resolved) {
                        co_return std::unexpected(resolved.error());
                    }

                    result.elements.push_back(*resolved);
                }
                co_return draft.intern_generic_type(std::move(result));
            },
            [&](const ASTSliceType& slice) noexcept -> AnalysisTask<GenericTypeID> {
                auto element = (co_await resolve_generic_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    slice.element_type,
                    context,
                    resolve_extent,
                    true,
                    requests,
                    observations
                ));
                if (!element) {
                    co_return std::unexpected(element.error());
                }
                co_return draft.intern_generic_type(GenericSliceType {.element = *element});
            },
            [&](const ASTPointerType& pointer) noexcept -> AnalysisTask<GenericTypeID> {
                auto target = (co_await resolve_generic_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    pointer.target,
                    context,
                    resolve_extent,
                    false,
                    requests,
                    observations
                ));
                if (!target) {
                    co_return std::unexpected(target.error());
                }
                co_return draft.intern_generic_type(
                    GenericPointerType {
                        .target = *target,
                        .access = pointer.access.mode == ASTAccessMode::Write ? PointerAccess::Write
                                                                              : PointerAccess::Read,
                    }
                );
            },
            [&](const ASTFunctionType&) noexcept -> AnalysisTask<GenericTypeID> {
                auto concrete = (co_await resolve_type_value(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    source,
                    resolve_extent,
                    requests,
                    &context,
                    observations
                ));
                if (!concrete) {
                    co_return std::unexpected(concrete.error());
                }
                co_return draft.intern_generic_type(draft.canonicalize_declared_type(*concrete));
            },
        }
    ));
}

auto resolve_generic_type_value(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTType& source,
    GenericTypeContext context,
    ArrayExtentResolver resolve_extent,
    bool value_required,
    ConstructionRequests* requests,
    SourceObservation* observations
) noexcept -> AnalysisTask<GenericTypeID> {
    auto result = co_await resolve_generic_type_contents(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        source,
        context,
        resolve_extent,
        value_required,
        requests,
        observations
    );
    const auto* named = std::get_if<ASTNamedType>(&source.value);
    if (result && observations && named) {
        observations->record(
            draft,
            locate(syntax.source_id(), named->components.back().name_span),
            std::nullopt,
            ConstructionTypeRef(*result)
        );
    }
    co_return result;
}

} // namespace

auto resolve_generic_source_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID source_type,
    GenericTypeContext context,
    ArrayExtentResolver resolve_extent,
    bool value_required,
    ConstructionRequests* requests,
    SourceObservation* observations
) noexcept -> AnalysisTask<GenericTypeID> {
    co_return co_await resolve_generic_type_value(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        syntax.type(source_type),
        context,
        resolve_extent,
        value_required,
        requests,
        observations
    );
}

auto resolve_source_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID source_type,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    if (generic_context) {
        auto resolved = co_await resolve_generic_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            source_type,
            *generic_context,
            resolve_extent,
            true,
            requests,
            observations
        );
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        const auto expression = draft.generic_type_copy(*resolved);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            co_return ConstructionTypeRef(*concrete);
        }
        co_return ConstructionTypeRef(*resolved);
    }
    co_return (co_await resolve_type_value(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        syntax.type(source_type),
        resolve_extent,
        requests,
        generic_context,
        observations
    ));
}

auto resolve_source_type_application(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTTypeApplicationExpr& application,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    auto operand = application.operand_id;
    auto components = std::vector<ASTTypeNameComponent>();
    auto global_root = std::optional<Span>();
    while (true) {
        const auto& expression = syntax.expression(operand);
        if (const auto* group = std::get_if<ASTGroupExpr>(&expression.value)) {
            operand = group->expression;
        } else if (const auto* member = std::get_if<ASTMemberExpr>(&expression.value);
                   member && member->op == ASTMemberOperator::Scope) {
            components.push_back({.name_span = member->name_span});
            operand = member->operand_id;
        } else if (const auto* name = std::get_if<ASTNameExpr>(&expression.value)) {
            components.push_back({.name_span = name->name_span});
            break;
        } else if (const auto* native = std::get_if<ASTCppNameExpr>(&expression.value)) {
            global_root = native->global_root;
            for (const auto component : std::views::reverse(native->components)) {
                components.push_back({.name_span = component});
            }
            break;
        } else {
            co_return std::unexpected(fail(
                draft,
                module_id,
                application.arguments_span,
                DiagnosticCode::TypeGenericArguments,
                "type application requires a declared type name"
            ));
        }
    }
    std::ranges::reverse(components);
    const auto named = ASTNamedType {
        .global_root = global_root,
        .components = std::move(components),
        .arguments = application.arguments,
    };
    const auto span = Span::from_bounds(
        syntax.expression(operand).span.start(),
        application.arguments_span.end()
    );
    if (generic_context) {
        const auto source = ASTType {.span = span, .value = named};
        auto resolved = co_await resolve_generic_type_value(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            source,
            *generic_context,
            resolve_extent,
            true,
            requests,
            observations
        );
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        const auto expression = draft.generic_type_copy(*resolved);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            co_return *concrete;
        }
        co_return *resolved;
    }
    co_return (co_await resolve_named(
        draft,
        catalog,
        import_usage,
        module_id,
        named,
        syntax,
        resolve_extent,
        span,
        requests,
        generic_context,
        observations
    ));
}

auto resolve_source_construction_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstructionType& source_type,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    if (generic_context) {
        const auto selected = source_type.value.visit([&](const auto& form) noexcept {
            return ASTType {.span = source_type.span, .value = form};
        });
        auto resolved = co_await resolve_generic_type_value(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            selected,
            *generic_context,
            resolve_extent,
            true,
            requests,
            observations
        );
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        const auto expression = draft.generic_type_copy(*resolved);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            co_return ConstructionTypeRef(*concrete);
        }
        co_return ConstructionTypeRef(*resolved);
    }
    co_return (co_await source_type.value.visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept -> AnalysisTask<ConstructionTypeRef> {
                co_return (co_await resolve_named(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    named,
                    syntax,
                    resolve_extent,
                    source_type.span,
                    requests,
                    generic_context,
                    observations
                ));
            },
            [&](const ASTFunctionType& function) noexcept -> AnalysisTask<ConstructionTypeRef> {
                co_return (co_await resolve_function_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    function,
                    resolve_extent,
                    requests,
                    generic_context,
                    observations
                ));
            },
        }
    ));
}

auto resolve_source_constraint_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstraintOperand& source_type,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    co_return (co_await source_type.value.visit(
        Overloaded {
            [&](const ASTQualifiedName& qualified) noexcept -> AnalysisTask<ConstructionTypeRef> {
                auto components = qualified.components
                    | std::views::transform([](Span span) static noexcept {
                                      return ASTTypeNameComponent {.name_span = span};
                                  })
                    | std::ranges::to<std::vector>();
                co_return (co_await resolve_named(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    ASTNamedType {
                        .global_root = std::nullopt,
                        .components = std::move(components),
                        .arguments = {}
                    },
                    syntax,
                    resolve_extent,
                    source_type.span,
                    requests,
                    generic_context,
                    observations
                ));
            },
            [&](const ASTArrayType& array) noexcept -> AnalysisTask<ConstructionTypeRef> {
                co_return (co_await resolve_type_value(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    ASTType {
                        .span = source_type.span,
                        .value = array,
                    },
                    resolve_extent,
                    requests,
                    generic_context,
                    observations
                ));
            },
        }
    ));
}

auto require_source_value_type(
    const ProgramDraft& draft,
    ConstructionTypeRef type,
    ProgramModuleID module_id,
    Span origin,
    std::string_view role
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    const auto* concrete = std::get_if<TypeID>(&type);
    if (concrete == nullptr) {
        return type;
    }
    const auto canonical = draft.type_copy(*concrete);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    if (builtin == nullptr || builtin->kind != BuiltinType::EntryArgs) {
        return type;
    }
    return std::unexpected(fail(
        draft,
        module_id,
        origin,
        DiagnosticCode::TypeValueRequired,
        std::format("{} requires a value type", role)
    ));
}

auto resolve_failure_types(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTThrowClause& clause,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context,
    SourceObservation* observations
) noexcept -> AnalysisTask<ConstructionFailureRef> {
    auto failures = std::vector<TypeID>();
    auto parameters = std::vector<ConstructionFailureRef>();
    auto first_seen = std::flat_map<TypeID, Span>();
    failures.reserve(clause.failures.size());
    for (const auto source_failure : clause.failures) {
        const auto& source = syntax.type(source_failure);
        if (generic_context) {
            const auto* name = std::get_if<ASTNamedType>(&source.value);
            if (name
                && !name->global_root
                && name->components.size() == 1uz
                && name->arguments.empty()) {
                const auto spelling =
                    draft.source_slice_copy(module_id, name->components.front().name_span);
                auto selected = std::optional<std::uint32_t>();
                for (auto index = 0uz; index < generic_context->parameters.size(); ++index) {
                    if (draft.spelling_copy(generic_context->parameters[index]) == spelling) {
                        selected = static_cast<std::uint32_t>(index);
                        break;
                    }
                }
                if (selected) {
                    const auto policy = generic_context->parameter_kinds[*selected];
                    if (policy != GenericParameterKind::FailureSet) {
                        co_return std::unexpected(fail(
                            draft,
                            module_id,
                            source.span,
                            DiagnosticCode::TypeGenericArguments,
                            "a failure clause requires a scalar failure-set parameter"
                        ));
                    }
                    parameters.push_back(draft.add_generic_failure_parameter(
                        {generic_context->definition, *selected}
                    ));
                    continue;
                }
            }
        }
        auto built = (co_await resolve_type_value(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            syntax.type(source_failure),
            resolve_extent,
            requests,
            generic_context,
            observations
        ));
        if (!built.has_value()) {
            co_return std::unexpected(built.error());
        }
        auto concrete = draft.require_failure_member(
            *built,
            draft.append_source_origin(
                draft.module_source(module_id),
                syntax.type(source_failure).span
            )
        );
        if (!concrete) {
            co_return std::unexpected(concrete.error());
        }
        if (const auto prior = first_seen.find(*concrete); prior != first_seen.end()) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::EffectThrowDuplicate,
                "failure clause contains the same nominal type more than once"
            );
            diagnostic.primary(
                locate(source_id(draft, module_id), syntax.type(source_failure).span),
                "duplicate failure type"
            );
            diagnostic.related(
                locate(source_id(draft, module_id), prior->second),
                "first occurrence"
            );
            co_return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        first_seen.emplace(*concrete, syntax.type(source_failure).span);
        failures.push_back(*concrete);
    }
    std::ranges::sort(failures, {}, &TypeID::index);
    const auto concrete = draft.intern_failure_set(std::move(failures));
    if (parameters.empty()) {
        co_return ConstructionFailureRef(concrete);
    }
    if (!draft.failure_set_copy(concrete).members.empty()) {
        parameters.push_back(concrete);
    }
    if (parameters.size() == 1uz) {
        co_return parameters.front();
    }
    co_return draft.union_generic_failures(std::move(parameters));
}

auto generic_parameter_index(
    const ProgramDraft& draft,
    const GenericTypeContext* context,
    std::string_view name
) noexcept -> std::optional<std::uint32_t> {
    if (context) {
        for (auto index = 0uz; index < context->parameters.size(); ++index) {
            if (draft.spelling(context->parameters[index]) == name) {
                return static_cast<std::uint32_t>(index);
            }
        }
    }
    return std::nullopt;
}
