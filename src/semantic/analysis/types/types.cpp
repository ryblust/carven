module carven:semantic.analysis.types.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.names;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :support.invariant;
import :support.visit;
import std;

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

auto builtin_kind(std::string_view name) noexcept -> std::optional<BuiltinType> {
    static constexpr auto names = std::array {
        std::pair {std::string_view("bool"), BuiltinType::Bool},
        std::pair {std::string_view("char"), BuiltinType::Char},
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
    };
    const auto* found = std::ranges::find(names, name, [](const auto& entry) static noexcept {
        return entry.first;
    });
    return found == names.end() ? std::nullopt : std::optional(found->second);
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
            std::format("unresolved type name '{}'", name)
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
    const ArrayExtentResolver& resolve_extent,
    Span origin
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    const auto root = draft.source_slice_copy(module_id, named.components.front().name_span);
    if (named.global_root.has_value()
        || (!builtin_kind(root).has_value() && catalog.lookup(module_id, root).empty())) {
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
            return std::unexpected(name.error());
        }
        if (name->has_value()) {
            auto arguments = std::vector<TypeID>();
            for (const auto argument : named.arguments) {
                auto resolved = resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    argument,
                    resolve_extent
                );
                if (!resolved.has_value()) {
                    return std::unexpected(resolved.error());
                }
                const auto* concrete = std::get_if<TypeID>(&*resolved);
                if (concrete == nullptr) {
                    return std::unexpected(fail(
                        draft,
                        module_id,
                        origin,
                        DiagnosticCode::TypeUnresolved,
                        "C++ type arguments require concrete types"
                    ));
                }
                arguments.push_back(*concrete);
            }
            return ConstructionTypeRef {draft.intern_type(
                {.value = CppTypeValue {
                     .form =
                         CppNamedType {.name = std::move(**name), .arguments = std::move(arguments)}
                 }}
            )};
        }
    }
    if (!named.arguments.empty()) {
        return std::unexpected(fail(
            draft,
            module_id,
            origin,
            DiagnosticCode::TypeUnresolved,
            "type arguments require an external C++ name"
        ));
    }
    if (named.components.size() != 1uz) {
        return std::unexpected(fail(
            draft,
            module_id,
            origin,
            DiagnosticCode::TypeUnresolved,
            "type names cannot contain value-member qualification"
        ));
    }
    const auto component = named.components.front().name_span;
    const auto name = draft.source_slice_copy(module_id, component);
    if (const auto builtin = builtin_kind(name)) {
        return ConstructionTypeRef {draft.intern_builtin_type(*builtin)};
    }
    const auto selected =
        select_global_symbol(draft, catalog, import_usage, module_id, name, component);
    if (!selected.has_value()) {
        return std::unexpected(selected.error());
    }
    if (const auto* structure = std::get_if<CatalogStructForm>(&(*selected)->form)) {
        return ConstructionTypeRef {draft.intern_type(
            CanonicalType {
                .value = StructTypeValue {.structure = structure->structure},
            }
        )};
    }
    if (const auto* enumeration = std::get_if<CatalogEnumForm>(&(*selected)->form)) {
        return ConstructionTypeRef {draft.intern_type(
            CanonicalType {
                .value = EnumTypeValue {.enumeration = enumeration->enumeration},
            }
        )};
    }
    return std::unexpected(fail(
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
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    auto parameters = std::vector<ConstructionCallableParameter>();
    parameters.reserve(function.parameters.size());
    for (const auto& parameter : function.parameters) {
        auto type = resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            parameter.type,
            resolve_extent
        );
        if (!type.has_value()) {
            return std::unexpected(type.error());
        }
        auto value_type = require_source_value_type(
            draft,
            *type,
            module_id,
            syntax.type(parameter.type).span,
            "function parameter"
        );
        if (!value_type.has_value()) {
            return std::unexpected(value_type.error());
        }
        parameters.push_back({
            .access = semantic_access_mode(parameter.access),
            .type = *value_type,
        });
    }
    auto result = resolve_source_type(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        function.result_type,
        resolve_extent
    );
    if (!result.has_value()) {
        return std::unexpected(result.error());
    }
    auto failures = std::vector<TypeID>();
    if (function.throw_clause.has_value()) {
        auto resolved = resolve_failure_types(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            *function.throw_clause,
            resolve_extent
        );
        if (!resolved.has_value()) {
            return std::unexpected(resolved.error());
        }
        failures = std::move(*resolved);
    }
    const auto failure_term = draft.add_concrete_failure_term(std::move(failures));
    return ConstructionTypeRef {draft.append_construction_type(
        ConstructionType {
            .value = ConstructionCallableViewTypeValue {
                .parameters = std::move(parameters),
                .result = *result,
                .failures = failure_term,
            },
        }
    )};
}

auto resolve_type_value(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTType& source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    return std::visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept {
                return resolve_named(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    named,
                    syntax,
                    resolve_extent,
                    source_type.span
                );
            },
            [&](const ASTArrayType& array) noexcept -> AnalysisResult<ConstructionTypeRef> {
                auto element = resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    array.element_type,
                    resolve_extent
                );
                if (!element.has_value()) {
                    return std::unexpected(element.error());
                }
                auto value_element = require_source_value_type(
                    draft,
                    *element,
                    module_id,
                    syntax.type(array.element_type).span,
                    "array element"
                );
                if (!value_element.has_value()) {
                    return std::unexpected(value_element.error());
                }
                auto extent = resolve_extent(array.extent);
                if (!extent.has_value()) {
                    return std::unexpected(extent.error());
                }
                if (const auto* concrete = std::get_if<TypeID>(&*value_element)) {
                    return ConstructionTypeRef {draft.intern_type(
                        CanonicalType {
                            .value = ArrayTypeValue {.element = *concrete, .extent = *extent},
                        }
                    )};
                }
                return ConstructionTypeRef {draft.append_construction_type(
                    ConstructionType {
                        .value = ConstructionArrayTypeValue {
                            .element = *value_element,
                            .extent = *extent,
                        },
                    }
                )};
            },
            [&](const ASTFunctionType& function) noexcept {
                return resolve_function_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    function,
                    resolve_extent
                );
            },
        },
        source_type.value
    );
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

auto resolve_source_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    ASTTypeID source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    return resolve_type_value(
        draft,
        catalog,
        import_usage,
        module_id,
        syntax,
        syntax.type(source_type),
        resolve_extent
    );
}

auto resolve_source_construction_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstructionType& source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    return std::visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept {
                return resolve_named(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    named,
                    syntax,
                    resolve_extent,
                    source_type.span
                );
            },
            [&](const ASTFunctionType& function) noexcept {
                return resolve_function_type(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    function,
                    resolve_extent
                );
            },
        },
        source_type.value
    );
}

auto resolve_source_constraint_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTConstraintOperand& source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    return std::visit(
        Overloaded {
            [&](const ASTQualifiedName& qualified) noexcept {
                auto components = qualified.components
                    | std::views::transform([](Span span) static noexcept {
                                      return ASTTypeNameComponent {.name_span = span};
                                  })
                    | std::ranges::to<std::vector>();
                return resolve_named(
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
                    source_type.span
                );
            },
            [&](const ASTArrayType& array) noexcept {
                return resolve_type_value(
                    draft,
                    catalog,
                    import_usage,
                    module_id,
                    syntax,
                    ASTType {
                        .span = source_type.span,
                        .value = array,
                    },
                    resolve_extent
                );
            },
        },
        source_type.value
    );
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
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<std::vector<TypeID>> {
    auto failures = std::vector<TypeID>();
    auto first_seen = std::flat_map<TypeID, Span>();
    failures.reserve(clause.failures.size());
    for (const auto source_failure : clause.failures) {
        auto built = resolve_source_type(
            draft,
            catalog,
            import_usage,
            module_id,
            syntax,
            source_failure,
            resolve_extent
        );
        if (!built.has_value()) {
            return std::unexpected(built.error());
        }
        const auto* concrete = std::get_if<TypeID>(&*built);
        const auto nominal = concrete == nullptr
            ? false
            : std::visit(
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
                                  || std::same_as<Value, CppTypeValue>,
                              "unhandled non-nominal failure type"
                          );
                          return false;
                      },
                  },
                  draft.type_copy(*concrete).value
              );
        if (!nominal) {
            return std::unexpected(fail(
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
            return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        first_seen.emplace(*concrete, syntax.type(source_failure).span);
        failures.push_back(*concrete);
    }
    std::ranges::sort(failures, {}, &TypeID::index);
    return failures;
}
