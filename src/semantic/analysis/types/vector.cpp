module carven:semantic.analysis.types.vector.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.limits;
import :semantic.analysis.program;
import :semantic.semir.generic;
import :semantic.semir.type;
import std;

namespace {

template<typename Shape>
auto unsupported_vector_element_shape(const Shape& value) noexcept
    -> std::optional<std::string_view> {
    if constexpr (std::same_as<Shape, BuiltinTypeValue>) {
        if (value.kind == BuiltinType::Str || value.kind == BuiltinType::StrCharsView) {
            return "Vector elements cannot contain borrowed text";
        }
    } else if constexpr (std::same_as<Shape, SliceTypeValue>
                         || std::same_as<Shape, GenericSliceType>
                         || std::same_as<Shape, ConstructionSliceTypeValue>) {
        return "Vector elements cannot contain borrowed slices";
    } else if constexpr (std::same_as<Shape, OperationTypeValue>
                         || std::same_as<Shape, GenericOperationType>
                         || std::same_as<Shape, ConstructionOperationTypeValue>) {
        return "Vector elements cannot contain async operations";
    } else if constexpr (std::same_as<Shape, CppTypeValue>) {
        if (!std::holds_alternative<CppNamedType>(value.form)) {
            return "Vector native elements require a named storage type";
        }
    } else if constexpr (std::same_as<Shape, GenericCppQueryType>) {
        return "Vector native elements require a named storage type";
    } else if constexpr (std::same_as<Shape, GenericClosureType>
                         || std::same_as<Shape, ClosureTypeValue>
                         || std::same_as<Shape, FunctionTypeValue>
                         || std::same_as<Shape, CallableViewTypeValue>
                         || std::same_as<Shape, ConstructionCallableViewTypeValue>
                         || std::same_as<Shape, GenericCallableViewType>) {
        return "Vector elements cannot contain callable values";
    }
    return std::nullopt;
}

auto unsupported_vector_element(const ProgramDraft& values, ConstructionTypeRef element) noexcept
    -> std::optional<std::string_view> {
    auto pending = std::vector<ConstructionTypeRef> {element};
    auto visited = std::set<ConstructionTypeRef>();
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        const auto inspect = [&](const auto& type) noexcept -> std::optional<std::string_view> {
            return type.value.visit(
                [&](const auto& shape) noexcept -> std::optional<std::string_view> {
                    if (const auto unsupported = unsupported_vector_element_shape(shape)) {
                        return unsupported;
                    }
                    using Shape = std::remove_cvref_t<decltype(shape)>;
                    if constexpr (std::same_as<Shape, ArrayTypeValue>
                                  || std::same_as<Shape, ConstructionArrayTypeValue>
                                  || std::same_as<Shape, VectorTypeValue>) {
                        pending.push_back(shape.element);
                    } else if constexpr (std::same_as<Shape, TupleTypeValue>
                                         || std::same_as<Shape, ConstructionTupleTypeValue>) {
                        pending.append_range(shape.elements);
                    } else if constexpr (std::same_as<Shape, StructTypeValue>) {
                        const auto fields = values.struct_field_types(shape.structure);
                        if (!fields) {
                            return "Vector element requires completed declaration fields";
                        }
                        pending.append_range(*fields);
                    } else if constexpr (std::same_as<Shape, EnumTypeValue>) {
                        const auto cases = values.enum_case_types(shape.enumeration);
                        if (!cases) {
                            return "Vector element requires completed enum payloads";
                        }
                        for (const auto& member : *cases) {
                            pending.append_range(member.payload_types);
                        }
                    }
                    // Raw pointers are values; their targets are not contained storage.
                    return std::nullopt;
                }
            );
        };
        const auto unsupported = std::holds_alternative<TypeID>(current)
            ? inspect(values.type_copy(std::get<TypeID>(current)))
            : inspect(values.construction_type_copy(std::get<TypeTermID>(current)));
        if (unsupported) {
            return unsupported;
        }
    }
    return std::nullopt;
}

} // namespace

// This check runs after source heads and the concrete instance closure are
// complete. It also checks fixed Vector elements in unused symbolic heads.
auto ProgramDraft::validate_vector_elements() noexcept -> AnalysisResult<void> {
    using Application = std::tuple<GenericDeclarationID, GenericEnvironment, bool>;
    auto visited = std::set<Application>();
    auto failure = std::optional<AnalysisFailure>();
    auto work = 0uz;
    const auto reject = [&](std::string_view reason, ProgramOriginID origin) noexcept {
        failure = diagnostics().error(
            DiagnosticBuilder(DiagnosticCode::TypeVectorElement, std::string(reason))
                .primary(source_span(origin))
                .build()
        );
    };
    // Canonical identity does not own source attribution. Keep each checked use
    // until all nominal fields are complete, then diagnose at that use.
    for (const auto& [element, origin] : storage.vector_elements) {
        if (const auto reason = unsupported_vector_element(*this, element)) {
            reject(*reason, origin);
            return std::unexpected(*failure);
        }
    }
    const auto visit = [&](this const auto& self,
                           ConstructionTypeRef type,
                           bool element_storage,
                           ProgramOriginID origin,
                           std::size_t depth) noexcept -> void {
        if (failure) {
            return;
        }
        if (++work > maximum_generic_type_work || depth >= maximum_generic_depth) {
            failure = diagnostics().error(DiagnosticBuilder(
                                              DiagnosticCode::TypeGenericLimits,
                                              "generic Vector element checking exceeded its budget"
            )
                                              .primary(source_span(origin))
                                              .build());
            return;
        }
        const auto* generic = std::get_if<GenericTypeID>(&type);
        if (!generic) {
            if (element_storage) {
                if (const auto reason = unsupported_vector_element(*this, type)) {
                    reject(*reason, origin);
                }
            }
            return;
        }
        generic_type_copy(*generic).visit([&](const auto& shape) noexcept {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, TypeID>) {
                if (element_storage) {
                    if (const auto reason = unsupported_vector_element(*this, shape)) {
                        reject(*reason, origin);
                    }
                }
            } else {
                if (element_storage) {
                    if (const auto reason = unsupported_vector_element_shape(shape)) {
                        reject(*reason, origin);
                        return;
                    }
                }
                if constexpr (std::same_as<Shape, GenericVectorType>) {
                    self(shape.element, true, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericArrayType>) {
                    self(shape.element, element_storage, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericTupleType>) {
                    for (const auto element : shape.elements) {
                        self(element, element_storage, origin, depth + 1uz);
                    }
                } else if constexpr (std::same_as<Shape, GenericSliceType>) {
                    self(shape.element, false, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericPointerType>) {
                    self(shape.target, false, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericCallableViewType>) {
                    for (const auto& parameter : shape.parameters) {
                        self(parameter.type, false, origin, depth + 1uz);
                    }
                    self(shape.result, false, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericNominalApplication>) {
                    const auto key =
                        Application {shape.definition, shape.arguments, element_storage};
                    if (!visited.insert(key).second) {
                        return;
                    }
                    for (const auto& argument : shape.arguments) {
                        visit_generic_argument_types(
                            argument,
                            [&](ConstructionTypeRef type) noexcept {
                                self(type, false, origin, depth + 1uz);
                            }
                        );
                    }
                    generic_declaration_copy(shape.definition)
                        .visit([&](const auto& definition) noexcept {
                            const auto member = [&](GenericTypeID type,
                                                    ProgramOriginID at) noexcept {
                                self(
                                    substitute_generic_type(
                                        type,
                                        shape.definition,
                                        shape.arguments
                                    ),
                                    element_storage,
                                    at,
                                    depth + 1uz
                                );
                            };
                            using Definition = std::remove_cvref_t<decltype(definition)>;
                            if constexpr (std::same_as<Definition, GenericRecordDefinition>) {
                                for (const auto& field : definition.fields) {
                                    member(field.type, field.origin);
                                }
                            } else if constexpr (std::same_as<Definition, GenericEnumDefinition>) {
                                for (const auto& item : definition.cases) {
                                    for (const auto type : item.payload_types) {
                                        member(type, item.origin);
                                    }
                                }
                            }
                        });
                }
            }
        });
    };
    for (auto index = 0uz; index < storage.generic_definitions.size(); ++index) {
        const auto owner =
            GenericDeclarationID(program_identity, static_cast<std::uint32_t>(index));
        const auto definition = generic_declaration_copy(owner);
        definition.visit([&](const auto& source) noexcept {
            if constexpr (std::same_as<
                              std::remove_cvref_t<decltype(source)>,
                              GenericCallableDefinition>) {
                const auto check = [&](ConstructionTypeRef type) noexcept {
                    if (const auto* symbolic = std::get_if<GenericTypeID>(&type)) {
                        visit(*symbolic, false, source.contract.origin, 0uz);
                    } else if (const auto* concrete = std::get_if<TypeID>(&type)) {
                        visit(intern_generic_type(*concrete), false, source.contract.origin, 0uz);
                    }
                };
                for (const auto& parameter : source.signature.parameters) {
                    check(parameter.type);
                }
                check(source.signature.result);
                return;
            }
            auto arguments = generic_parameter_environment(owner);
            visit(
                intern_generic_type(
                    GenericNominalApplication {
                        .definition = owner,
                        .arguments = std::move(arguments)
                    }
                ),
                false,
                source.contract.origin,
                0uz
            );
        });
    }
    return failure ? AnalysisResult<void>(std::unexpected(*failure)) : AnalysisResult<void>();
}
