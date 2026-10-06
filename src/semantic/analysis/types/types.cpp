module carven:semantic.analysis.types.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.suggestion;
import :semantic.analysis.names;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :support.invariant;
import :support.visit;
import std;

auto source_builtin_type(std::string_view name) noexcept -> std::optional<BuiltinType> {
    static constexpr auto names = std::array {
        std::pair {std::string_view("bool"), BuiltinType::Bool},
        std::pair {std::string_view("char"), BuiltinType::Char},
        std::pair {std::string_view("String"), BuiltinType::String},
        std::pair {std::string_view("str"), BuiltinType::Str},
        std::pair {std::string_view("i8"), BuiltinType::I8},
        std::pair {std::string_view("i16"), BuiltinType::I16},
        std::pair {std::string_view("i32"), BuiltinType::I32},
        std::pair {std::string_view("i64"), BuiltinType::I64},
        std::pair {std::string_view("u8"), BuiltinType::U8},
        std::pair {std::string_view("u16"), BuiltinType::U16},
        std::pair {std::string_view("u32"), BuiltinType::U32},
        std::pair {std::string_view("u64"), BuiltinType::U64},
        std::pair {std::string_view("isize"), BuiltinType::Isize},
        std::pair {std::string_view("usize"), BuiltinType::Usize},
        std::pair {std::string_view("f32"), BuiltinType::F32},
        std::pair {std::string_view("f64"), BuiltinType::F64},
        std::pair {std::string_view("void"), BuiltinType::Void},
        std::pair {std::string_view("u8x32"), BuiltinType::U8x32},
        std::pair {std::string_view("mask32"), BuiltinType::Mask32},
        std::pair {std::string_view("f32x8"), BuiltinType::F32x8},
        std::pair {std::string_view("mask8"), BuiltinType::Mask8},
        std::pair {std::string_view("u8x16"), BuiltinType::U8x16},
        std::pair {std::string_view("mask16"), BuiltinType::Mask16},
        std::pair {std::string_view("f32x4"), BuiltinType::F32x4},
        std::pair {std::string_view("mask4"), BuiltinType::Mask4},
    };
    const auto found = std::ranges::find(names, name, [](const auto& entry) static noexcept {
        return entry.first;
    });
    return found == names.end() ? std::nullopt : std::optional(found->second);
}

auto source_type_name_is_reserved(std::string_view name) noexcept -> bool {
    return name == "ptr" || name == "range" || name == "Sequence" || source_builtin_type(name);
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

auto select_global_symbol(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    std::string_view name,
    Span origin
) noexcept -> AnalysisResult<const CatalogSymbol*> {
    const auto candidates = catalog.lookup(module_id, name);
    if (candidates.empty()) {
        return std::unexpected(fail(
            draft,
            module_id,
            origin,
            DiagnosticCode::TypeUnresolved,
            std::format(
                "unresolved type name '{}'{}",
                name,
                spelling_suggestion(name, catalog.visible_names(module_id))
            )
        ));
    }
    if (candidates.size() != 1uz) {
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::NameAmbiguous,
            std::format("name '{}' is provided by more than one wildcard import", name)
        );
        diagnostic.primary(locate(source_id(draft, module_id), origin), "ambiguous reference");
        for (const auto& candidate : candidates) {
            const auto* symbol = catalog.symbol(candidate.symbol_id);
            if (symbol == nullptr) {
                invariant_violation("catalog lookup returned an invalid symbol identity");
            }
            diagnostic.related(
                locate(source_id(draft, symbol->module_id), symbol->declaration_span),
                std::format(
                    "candidate from '{}'",
                    draft.module_path_copy(symbol->module_id).value()
                )
            );
        }
        return std::unexpected(draft.diagnostics().error(diagnostic.build()));
    }
    const auto& selected = candidates.front();
    if (selected.import_binding.has_value()) {
        import_usage.record(*selected.import_binding);
    }
    const auto* symbol = catalog.symbol(selected.symbol_id);
    if (symbol == nullptr) {
        invariant_violation("catalog lookup returned an invalid symbol identity");
    }
    return symbol;
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
    const GenericTypeContext* generic_context
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    const auto root = draft.source_slice_copy(module_id, named.components.front().name_span);
    if (generic_context
        && !named.global_root
        && std::ranges::any_of(
            generic_context->parameters,
            [&](ProgramSpellingID parameter) noexcept {
                return draft.spelling_copy(parameter) == root;
            }
        )) {
        co_return std::unexpected(fail(
            draft,
            module_id,
            origin,
            DiagnosticCode::TypeGenericDefinition,
            "this type position requires a concrete type rather than a type parameter"
        ));
    }
    if (!named.global_root && named.components.size() == 1uz && root == "Sequence") {
        if (named.arguments.size() != 1uz) {
            co_return std::unexpected(fail(
                draft,
                module_id,
                origin,
                DiagnosticCode::TypeUnresolved,
                "Sequence requires one element type"
            ));
        }
        auto element = (co_await resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            named.arguments.front(),
            resolve_extent,
            requests,
            generic_context
        ));
        if (!element) {
            co_return std::unexpected(element.error());
        }
        if (auto checked =
                require_source_value_type(draft, *element, module_id, origin, "Sequence element");
            !checked) {
            co_return std::unexpected(checked.error());
        }
        const auto concrete = draft.canonicalize_declared_type(*element);
        draft.record_sequence_element(
            concrete,
            draft.append_source_origin(draft.module_source(module_id), origin)
        );
        co_return ConstructionTypeRef {
            draft.intern_type({.value = OwnedSequenceTypeValue {.element = concrete}})
        };
    }
    if (!named.global_root && named.components.size() == 1uz && root == "range") {
        if (named.arguments.size() != 1uz) {
            co_return std::unexpected(fail(
                draft,
                module_id,
                origin,
                DiagnosticCode::TypeUnresolved,
                "range requires one integer element type"
            ));
        }
        auto element = (co_await resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            named.arguments.front(),
            resolve_extent,
            requests,
            generic_context
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
    if (named.global_root.has_value()
        || (!source_builtin_type(root).has_value() && catalog.lookup(module_id, root).empty())) {
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
            for (const auto argument : named.arguments) {
                auto resolved = (co_await resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    argument,
                    resolve_extent,
                    requests,
                    generic_context
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
    if (const auto builtin = source_builtin_type(name)) {
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
    const auto selected =
        select_global_symbol(draft, catalog, import_usage, module_id, name, component);
    if (!selected.has_value()) {
        co_return std::unexpected(selected.error());
    }
    if (const auto* generic = std::get_if<CatalogGenericForm>(&(*selected)->form)) {
        if (requests) {
            auto ready =
                (co_await requests->ensure_declaration((*selected)->symbol_id, module_id, origin));
            if (!ready) {
                co_return std::unexpected(ready.error());
            }
        }
        auto arguments = std::vector<TypeID>();
        for (const auto argument : named.arguments) {
            auto resolved = (co_await resolve_source_type(
                draft,
                catalog,
                import_usage,
                module_id,
                syntax,
                argument,
                resolve_extent,
                requests,
                generic_context
            ));
            if (!resolved) {
                co_return std::unexpected(resolved.error());
            }
            arguments.push_back(draft.canonicalize_declared_type(*resolved));
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
    if (const auto* structure = std::get_if<CatalogStructForm>(&(*selected)->form)) {
        co_return ConstructionTypeRef {draft.intern_type(
            CanonicalType {
                .value = StructTypeValue {.structure = structure->structure},
            }
        )};
    }
    if (const auto* enumeration = std::get_if<CatalogEnumForm>(&(*selected)->form)) {
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

auto resolve_function_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTFunctionType& function,
    ArrayExtentResolver resolve_extent,
    ConstructionRequests* requests,
    const GenericTypeContext* generic_context
) noexcept -> AnalysisTask<ConstructionTypeRef> {
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
            generic_context
        ));
        if (!type.has_value()) {
            co_return std::unexpected(type.error());
        }
        auto value_type = require_source_value_type(
            draft,
            *type,
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
        generic_context
    ));
    if (!result.has_value()) {
        co_return std::unexpected(result.error());
    }
    auto failures = std::vector<TypeID>();
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
            generic_context
        ));
        if (!resolved.has_value()) {
            co_return std::unexpected(resolved.error());
        }
        failures = std::move(*resolved);
    }
    const auto failure_term = draft.add_known_failure_term(std::move(failures));
    const auto bound = draft.append_construction_type(
        ConstructionType {
            .value = ConstructionCallableViewTypeValue {
                .parameters = std::move(parameters),
                .result = *result,
                .failures = failure_term,
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
    const GenericTypeContext* generic_context
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
                    generic_context
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
                    generic_context
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
                    generic_context
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
                    generic_context
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
                    generic_context
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
    ConstructionRequests* requests
) noexcept -> AnalysisTask<GenericTypeID> {
    const auto& source = syntax.type(source_type);
    const auto parameter_index =
        [&](std::string_view name) noexcept -> std::optional<std::uint32_t> {
        for (auto index = 0uz; index < context.parameters.size(); ++index) {
            if (draft.spelling(context.parameters[index]) == name) {
                return static_cast<std::uint32_t>(index);
            }
        }
        return std::nullopt;
    };
    co_return (co_await source.value.visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept -> AnalysisTask<GenericTypeID> {
                if (!named.global_root && named.components.size() == 1uz) {
                    const auto component = named.components.front().name_span;
                    const auto name = draft.source_slice_copy(module_id, component);
                    if (const auto index = parameter_index(name)) {
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
                    if (name == "Sequence") {
                        if (named.arguments.size() != 1uz) {
                            co_return std::unexpected(fail(
                                draft,
                                module_id,
                                source.span,
                                DiagnosticCode::TypeUnresolved,
                                "Sequence requires one element type"
                            ));
                        }
                        auto element = (co_await resolve_generic_source_type(
                            draft,
                            catalog,
                            import_usage,
                            module_id,
                            syntax,
                            named.arguments.front(),
                            context,
                            resolve_extent,
                            true,
                            requests
                        ));
                        if (!element) {
                            co_return std::unexpected(element.error());
                        }
                        co_return draft.intern_generic_type(
                            GenericOwnedSequenceType {.element = *element}
                        );
                    }
                    if (!catalog.lookup(module_id, name).empty()) {
                        const auto selected = select_global_symbol(
                            draft,
                            catalog,
                            import_usage,
                            module_id,
                            name,
                            component
                        );
                        if (!selected) {
                            co_return std::unexpected(selected.error());
                        }
                        if (const auto* generic =
                                std::get_if<CatalogGenericForm>(&(*selected)->form)) {
                            auto arguments = std::vector<GenericTypeID>();
                            for (const auto argument : named.arguments) {
                                auto resolved = (co_await resolve_generic_source_type(
                                    draft,
                                    catalog,
                                    import_usage,
                                    module_id,
                                    syntax,
                                    argument,
                                    context,
                                    resolve_extent,
                                    true,
                                    requests
                                ));
                                if (!resolved) {
                                    co_return std::unexpected(resolved.error());
                                }
                                arguments.push_back(*resolved);
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
                auto concrete = (co_await resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    source_type,
                    resolve_extent,
                    requests,
                    &context
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
                    requests
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
                    requests
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
                    requests
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
                auto concrete = (co_await resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    source_type,
                    resolve_extent,
                    requests,
                    &context
                ));
                if (!concrete) {
                    co_return std::unexpected(concrete.error());
                }
                co_return draft.intern_generic_type(draft.canonicalize_declared_type(*concrete));
            },
        }
    ));
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
    const GenericTypeContext* generic_context
) noexcept -> AnalysisTask<ConstructionTypeRef> {
    co_return (co_await resolve_type_value(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        syntax.type(source_type),
        resolve_extent,
        requests,
        generic_context
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
    const GenericTypeContext* generic_context
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
    co_return (co_await resolve_named(
        draft,
        catalog,
        import_usage,
        module_id,
        named,
        syntax,
        resolve_extent,
        Span::from_bounds(
            syntax.expression(operand).span.start(),
            application.arguments_span.end()
        ),
        requests,
        generic_context
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
    const GenericTypeContext* generic_context
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
                    generic_context
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
                    generic_context
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
    const GenericTypeContext* generic_context
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
                    generic_context
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
                    generic_context
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
    if (builtin == nullptr
        || (builtin->kind != BuiltinType::Void && builtin->kind != BuiltinType::EntryArgs)) {
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
    const GenericTypeContext* generic_context
) noexcept -> AnalysisTask<std::vector<TypeID>> {
    auto failures = std::vector<TypeID>();
    auto first_seen = std::flat_map<TypeID, Span>();
    failures.reserve(clause.failures.size());
    for (const auto source_failure : clause.failures) {
        auto built = (co_await resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            source_failure,
            resolve_extent,
            requests,
            generic_context
        ));
        if (!built.has_value()) {
            co_return std::unexpected(built.error());
        }
        const auto* concrete = std::get_if<TypeID>(&*built);
        const auto nominal = concrete == nullptr
            ? false
            : draft.type_copy(*concrete).value.visit(
                  Overloaded {
                      [](const StructTypeValue&) static noexcept { return true; },
                      [](const EnumTypeValue&) static noexcept { return true; },
                      []<typename Value>(const Value&) static noexcept {
                          static_assert(
                              std::same_as<Value, BuiltinTypeValue>
                                  || std::same_as<Value, ArrayTypeValue>
                                  || std::same_as<Value, FunctionTypeValue>
                                  || std::same_as<Value, ClosureTypeValue>
                                  || std::same_as<Value, CallableViewTypeValue>
                                  || std::same_as<Value, CppTypeValue>
                                  || std::same_as<Value, PointerTypeValue>
                                  || std::same_as<Value, OwnedSequenceTypeValue>
                                  || std::same_as<Value, SliceTypeValue>
                                  || std::same_as<Value, RangeTypeValue>,
                              "unhandled non-nominal failure type"
                          );
                          return false;
                      },
                  }
              );
        if (!nominal) {
            co_return std::unexpected(fail(
                draft,
                module_id,
                syntax.type(source_failure).span,
                DiagnosticCode::EffectThrowType,
                "failure clause entries must be copyable nominal struct or enum types"
            ));
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
    co_return failures;
}
