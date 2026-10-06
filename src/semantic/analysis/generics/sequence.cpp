module carven:semantic.analysis.generics.sequence.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.limits;
import :semantic.analysis.program;
import :semantic.semir.sequence;
import std;

// This check runs after source heads and the concrete instance closure are
// complete. It also checks fixed sequence elements in unused symbolic heads.
auto ProgramDraft::validate_generic_sequence_elements() noexcept -> AnalysisResult<void> {
    using Application = std::tuple<GenericDeclarationID, std::vector<GenericTypeID>, bool>;
    auto visited = std::set<Application>();
    auto failure = std::optional<AnalysisFailure>();
    auto work = 0uz;
    const auto reject = [&](std::string_view reason, ProgramOriginID origin) noexcept {
        failure = diagnostics().error(
            DiagnosticBuilder(DiagnosticCode::TypeSequenceElement, std::string(reason))
                .primary(source_span(origin))
                .build()
        );
    };
    // Canonical identity does not own source attribution. Keep each checked use
    // until all nominal fields are complete, then diagnose at that use.
    for (const auto& [element, origin] : storage.sequence_elements) {
        if (const auto reason = unsupported_sequence_element(*this, element)) {
            reject(*reason, origin);
            return std::unexpected(*failure);
        }
    }
    const auto visit = [&](this const auto& self,
                           GenericTypeID type,
                           bool element_storage,
                           ProgramOriginID origin,
                           std::size_t depth) noexcept -> void {
        if (failure) {
            return;
        }
        if (++work > maximum_generic_type_work || depth >= maximum_generic_depth) {
            failure =
                diagnostics().error(DiagnosticBuilder(
                                        DiagnosticCode::TypeGenericLimits,
                                        "generic Sequence element checking exceeded its budget"
                )
                                        .primary(source_span(origin))
                                        .build());
            return;
        }
        generic_type_copy(type).visit([&](const auto& shape) noexcept {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, TypeID>) {
                if (element_storage) {
                    if (const auto reason = unsupported_sequence_element(*this, shape)) {
                        reject(*reason, origin);
                    }
                }
            } else {
                if (element_storage) {
                    if (const auto reason = unsupported_sequence_element_shape(shape)) {
                        reject(*reason, origin);
                        return;
                    }
                }
                if constexpr (std::same_as<Shape, GenericOwnedSequenceType>) {
                    self(shape.element, true, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericArrayType>) {
                    self(shape.element, element_storage, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericSliceType>) {
                    self(shape.element, false, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericPointerType>) {
                    self(shape.target, false, origin, depth + 1uz);
                } else if constexpr (std::same_as<Shape, GenericNominalApplication>) {
                    const auto key =
                        Application {shape.definition, shape.arguments, element_storage};
                    if (!visited.insert(key).second) {
                        return;
                    }
                    for (const auto argument : shape.arguments) {
                        self(argument, false, origin, depth + 1uz);
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
                            } else {
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
            auto arguments = std::vector<GenericTypeID>();
            for (auto parameter = 0uz; parameter < source.contract.parameters.size(); ++parameter) {
                arguments.push_back(intern_generic_type(
                    GenericTypeParameter {owner, static_cast<std::uint32_t>(parameter)}
                ));
            }
            visit(
                intern_generic_type(GenericNominalApplication {owner, std::move(arguments)}),
                false,
                source.contract.origin,
                0uz
            );
        });
    }
    return failure ? AnalysisResult<void>(std::unexpected(*failure)) : AnalysisResult<void>();
}
