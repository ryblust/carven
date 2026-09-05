module carven:semantic.analysis.types.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.types;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto source_id(const ProgramDraft& draft, ProgramModuleID module) noexcept -> SourceID {
    return draft.syntax_tree(module).view().source_id();
}

auto fail(
    const ProgramDraft& draft,
    ProgramModuleID module,
    Span span,
    DiagnosticCode code,
    std::string message
) noexcept -> AnalysisFailure {
    return draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                         .primary(locate(source_id(draft, module), span))
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
    const auto* const found = std::ranges::find(names, name, [](const auto& entry) static noexcept {
        return entry.first;
    });
    return found == names.end() ? std::nullopt : std::optional(found->second);
}

auto select_global_symbol(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module,
    std::string_view name,
    Span origin
) noexcept -> AnalysisResult<const CatalogSymbol*> {
    const auto candidates = catalog.lookup(module, name);
    if (candidates.empty()) {
        return std::unexpected(fail(
            draft,
            module,
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
        diagnostic.primary(locate(source_id(draft, module), origin), "ambiguous reference");
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
    ProgramModuleID module,
    const ASTNamedType& named,
    Span origin
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    if (named.components.size() != 1uz) {
        return std::unexpected(fail(
            draft,
            module,
            origin,
            DiagnosticCode::TypeUnresolved,
            "type names cannot contain value-member qualification"
        ));
    }
    const auto component = named.components.front().name_span;
    const auto name = draft.source_slice_copy(module, component);
    if (const auto builtin = builtin_kind(name)) {
        return ConstructionTypeRef {draft.intern_builtin_type(*builtin)};
    }
    const auto selected =
        select_global_symbol(draft, catalog, import_usage, module, name, component);
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
        module,
        origin,
        DiagnosticCode::TypeUnresolved,
        std::format("'{}' does not name a type", name)
    ));
}

auto resolve_function_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module,
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
            module,
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
            module,
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
        module,
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
            module,
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
    ProgramModuleID module,
    ASTView syntax,
    const ASTType& source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    return std::visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept {
                return resolve_named(draft, catalog, import_usage, module, named, source_type.span);
            },
            [&](const ASTArrayType& array) noexcept -> AnalysisResult<ConstructionTypeRef> {
                auto element = resolve_source_type(
                    draft,
                    catalog,
                    import_usage,
                    module,
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
                    module,
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
                    module,
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
    ProgramModuleID module,
    ASTView syntax,
    ASTTypeID source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    return resolve_type_value(
        draft,
        catalog,
        import_usage,
        module,
        syntax,
        syntax.type(source_type),
        resolve_extent
    );
}

auto resolve_source_construction_type(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module,
    ASTView syntax,
    const ASTConstructionType& source_type,
    const ArrayExtentResolver& resolve_extent
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    return std::visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept {
                return resolve_named(draft, catalog, import_usage, module, named, source_type.span);
            },
            [&](const ASTFunctionType& function) noexcept {
                return resolve_function_type(
                    draft,
                    catalog,
                    import_usage,
                    module,
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
    ProgramModuleID module,
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
                    module,
                    ASTNamedType {.components = std::move(components)},
                    source_type.span
                );
            },
            [&](const ASTArrayType& array) noexcept {
                return resolve_type_value(
                    draft,
                    catalog,
                    import_usage,
                    module,
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
    ProgramModuleID module,
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
        module,
        origin,
        DiagnosticCode::TypeValueRequired,
        std::format("{} requires a value type", role)
    ));
}

auto resolve_failure_types(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage,
    ProgramModuleID module,
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
            module,
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
                                  || std::same_as<Value, CallableViewTypeValue>,
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
                module,
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
                locate(source_id(draft, module), syntax.type(source_failure).span),
                "duplicate failure type"
            );
            diagnostic.related(locate(source_id(draft, module), prior->second), "first occurrence");
            return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        first_seen.emplace(*concrete, syntax.type(source_failure).span);
        failures.push_back(*concrete);
    }
    std::ranges::sort(failures, {}, &TypeID::index);
    return failures;
}
