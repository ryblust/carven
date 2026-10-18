module carven:semantic.analysis.operations.structure.impl;

import :diagnostics.builder;
import :diagnostics.suggestion;
import :semantic.analysis.operations;
import :semantic.semir.generic;
import :support.invariant;
import std;

auto select_structure_initializers(
    const ProgramDraft& draft,
    ProgramModuleID module_id,
    const ASTConstructionInitializer& source,
    std::span<const ConstructionStructField> fields
) noexcept -> AnalysisResult<std::vector<StructureInitializer>> {
    const auto syntax = draft.syntax_tree(module_id).view();
    const auto fail = [&](Span span, DiagnosticCode code, std::string message) noexcept {
        return std::unexpected(
            draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                          .primary(locate(syntax.source_id(), span))
                                          .build())
        );
    };
    auto result = std::vector<StructureInitializer>();
    if (const auto* positional = std::get_if<ASTPositionalInitializerList>(&source.value)) {
        if (positional->values.size() != fields.size()) {
            return fail(
                positional->span,
                DiagnosticCode::TypeConstructArity,
                "positional initializer count differs from structure fields"
            );
        }
        for (auto index = 0uz; index < positional->values.size(); ++index) {
            result.push_back(
                {.declaration_index = static_cast<std::uint32_t>(index),
                 .expression = positional->values[index],
                 .name = std::nullopt}
            );
        }
        return result;
    }
    if (const auto* named = std::get_if<ASTFieldInitializerList>(&source.value)) {
        auto initialized = std::vector<bool>(fields.size());
        for (const auto& field : named->fields) {
            const auto name = draft.source_slice_copy(module_id, field.name_span);
            const auto found = std::ranges::find_if(fields, [&](const auto& candidate) noexcept {
                return draft.spelling_copy(candidate.name) == name;
            });
            if (found == fields.end()) {
                auto names = std::vector<std::string>();
                for (const auto& candidate : fields) {
                    names.push_back(draft.spelling_copy(candidate.name));
                }
                return fail(
                    field.name_span,
                    DiagnosticCode::TypeConstructUnknownField,
                    std::format(
                        "structure has no field named '{}'{}",
                        name,
                        spelling_suggestion(name, names)
                    )
                );
            }
            const auto index = static_cast<std::size_t>(found - fields.begin());
            if (initialized[index]) {
                return fail(
                    field.name_span,
                    DiagnosticCode::TypeConstructDuplicateField,
                    std::format("field '{}' is initialized more than once", name)
                );
            }
            initialized[index] = true;
            result.push_back(
                {.declaration_index = static_cast<std::uint32_t>(index),
                 .expression = field.value,
                 .name = field.name_span}
            );
        }
        for (auto index = 0uz; index < fields.size(); ++index) {
            if (!initialized[index]) {
                return fail(
                    named->span,
                    DiagnosticCode::TypeConstructArity,
                    std::format(
                        "missing initializer for field '{}'",
                        draft.spelling_copy(fields[index].name)
                    )
                );
            }
        }
        return result;
    }
    invariant_violation("structure initializer selection requires a nonempty construction");
}

auto default_initialization(ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> DefaultInitialization {
    return query_default_initialization(
        unwrap_type_alias(draft, type),
        [&](ConstructionTypeRef reference) noexcept
            -> std::variant<CanonicalType, ConstructionType, GenericTypeExpression> {
            if (const auto* concrete = std::get_if<TypeID>(&reference)) {
                return draft.type_copy(*concrete);
            }
            if (const auto* generic = std::get_if<GenericTypeID>(&reference)) {
                return draft.generic_type_copy(*generic);
            }
            return draft.construction_type_copy(std::get<TypeTermID>(reference));
        },
        [&](ConstructionTypeRef reference, const auto& shape) noexcept {
            if constexpr (std::same_as<std::remove_cvref_t<decltype(shape)>, StructTypeValue>) {
                return draft.construction_struct_declaration_copy(shape.structure);
            } else {
                return record_shape(draft, reference);
            }
        }
    );
}

// Nominal fields are checked against the declaration's rigid parameter environment.
auto record_shape(ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<RecordShape> {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*concrete);
        const auto* record = std::get_if<StructTypeValue>(&canonical.value);
        if (!record) {
            return std::nullopt;
        }
        auto declaration = draft.construction_struct_declaration_copy(record->structure);
        return RecordShape {
            .owner = record->structure,
            .kind = declaration.kind,
            .name = declaration.name,
            .fields = std::move(declaration.fields)
        };
    }
    const auto* generic = std::get_if<GenericTypeID>(&type);
    if (!generic) {
        return std::nullopt;
    }
    const auto expression = draft.generic_type_copy(*generic);
    if (const auto* concrete = std::get_if<TypeID>(&expression)) {
        return record_shape(draft, *concrete);
    }
    const auto* application = std::get_if<GenericNominalApplication>(&expression);
    if (!application) {
        return std::nullopt;
    }
    const auto definition = draft.generic_declaration_copy(application->definition);
    const auto* record = std::get_if<GenericRecordDefinition>(&definition);
    if (!record) {
        return std::nullopt;
    }
    auto fields = std::vector<ConstructionStructField>();
    auto types = draft.generic_member_types(*generic);
    if (!types) {
        types = std::vector<ConstructionTypeRef>();
        for (const auto& field : record->fields) {
            types->push_back(draft.substitute_generic_type(
                field.type,
                application->definition,
                application->arguments
            ));
        }
        draft.cache_generic_record_fields(*generic, *types);
    }
    for (const auto& [field, type] : std::views::zip(record->fields, *types)) {
        fields.push_back({field.name, type, field.origin});
    }
    return RecordShape {
        .owner = *generic,
        .kind = record->kind,
        .name = record->contract.name,
        .fields = std::move(fields)
    };
}

// Copy is a value construction capability, independently of Read forwarding.
auto type_supports_copy(ProgramDraft& draft, ConstructionTypeRef type) noexcept -> bool {
    auto pending = std::vector<ConstructionTypeRef> {type};
    auto seen = std::flat_set<ConstructionTypeRef>();
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!seen.insert(current).second) {
            continue;
        }
        if (const auto* generic = std::get_if<GenericTypeID>(&current)) {
            const auto expression = draft.generic_type_copy(*generic);
            if (const auto* concrete = std::get_if<TypeID>(&expression)) {
                pending.push_back(*concrete);
            } else if (std::holds_alternative<GenericTypeParameter>(expression)
                       || std::holds_alternative<GenericOperationType>(expression)) {
                return false;
            } else if (const auto* array = std::get_if<GenericArrayType>(&expression)) {
                if (array->extent != 0u) {
                    pending.push_back(array->element);
                }
            } else if (const auto* tuple = std::get_if<GenericTupleType>(&expression)) {
                for (const auto element : tuple->elements) {
                    pending.push_back(element);
                }
            } else if (const auto* sequence = std::get_if<GenericVectorType>(&expression)) {
                pending.push_back(sequence->element);
            } else if (const auto* application =
                           std::get_if<GenericNominalApplication>(&expression)) {
                const auto definition = draft.generic_declaration_copy(application->definition);
                if (const auto* record = std::get_if<GenericRecordDefinition>(&definition)) {
                    for (const auto& field : record->fields) {
                        pending.push_back(draft.substitute_generic_type(
                            field.type,
                            application->definition,
                            application->arguments
                        ));
                    }
                } else if (const auto* enumeration =
                               std::get_if<GenericEnumDefinition>(&definition)) {
                    for (const auto& item : enumeration->cases) {
                        for (const auto field : item.payload_types) {
                            pending.push_back(draft.substitute_generic_type(
                                field,
                                application->definition,
                                application->arguments
                            ));
                        }
                    }
                } else {
                    return false;
                }
            }
            continue;
        }
        if (const auto* term = std::get_if<TypeTermID>(&current)) {
            const auto value = draft.construction_type_copy(*term);
            if (std::holds_alternative<ConstructionOperationTypeValue>(value.value)) {
                return false;
            }
            if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&value.value)) {
                if (array->extent != 0u) {
                    pending.push_back(array->element);
                }
            } else if (const auto* tuple = std::get_if<ConstructionTupleTypeValue>(&value.value)) {
                pending.append_range(tuple->elements);
            }
            continue;
        }
        const auto value = draft.type_copy(std::get<TypeID>(current));
        if (std::holds_alternative<OperationTypeValue>(value.value)) {
            return false;
        }
        if (const auto* array = std::get_if<ArrayTypeValue>(&value.value)) {
            if (array->extent != 0u) {
                pending.push_back(array->element);
            }
        } else if (const auto* tuple = std::get_if<TupleTypeValue>(&value.value)) {
            for (const auto element : tuple->elements) {
                pending.push_back(element);
            }
        } else if (const auto* sequence = std::get_if<VectorTypeValue>(&value.value)) {
            pending.push_back(sequence->element);
        } else if (const auto* record = std::get_if<StructTypeValue>(&value.value)) {
            for (const auto& field :
                 draft.construction_struct_declaration_copy(record->structure).fields) {
                pending.push_back(field.type);
            }
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&value.value)) {
            for (const auto item : draft.enum_declaration_copy(enumeration->enumeration).cases) {
                for (const auto element :
                     draft.construction_enum_case_declaration_copy(item).payload_types) {
                    pending.push_back(element);
                }
            }
        }
    }
    return true;
}
