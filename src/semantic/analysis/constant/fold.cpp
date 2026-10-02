module carven:semantic.analysis.constant.fold.impl;

import :diagnostics.builder;
import :semantic.analysis.constant.fold;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.simd;
import std;

auto array_constant(ProgramDraft& draft, TypeID type, const SemArray& array) noexcept
    -> std::optional<ConstantID> {
    auto elements = std::vector<ConstantID>();
    for (const auto& element : array.elements) {
        if (!element.constant) {
            return std::nullopt;
        }
        elements.push_back(*element.constant);
    }
    return draft.intern_constant(
        {.type = type, .value = ArrayConstant {.elements = std::move(elements)}}
    );
}

auto struct_constant(ProgramDraft& draft, TypeID type, const SemStruct& structure) noexcept
    -> std::optional<ConstantID> {
    if (draft.construction_struct_declaration_copy(structure.structure).kind == RecordKind::Class) {
        return std::nullopt;
    }
    auto fields = std::vector<std::optional<ConstantID>>(structure.fields.size());
    for (const auto& field : structure.fields) {
        fields[field.declaration_index] = field.value.constant;
    }
    auto values = std::vector<ConstantID>();
    for (const auto& field : fields) {
        if (!field) {
            return std::nullopt;
        }
        values.push_back(*field);
    }
    return draft.intern_constant(
        {.type = type, .value = StructConstant {.fields = std::move(values)}}
    );
}

auto field_constant(
    const ProgramDraft& draft,
    std::optional<ConstantID> source,
    std::size_t field_index
) noexcept -> std::optional<ConstantID> {
    if (!source) {
        return std::nullopt;
    }
    const auto* structure = std::get_if<StructConstant>(&draft.constant(*source).value);
    return structure ? std::optional(structure->fields.at(field_index)) : std::nullopt;
}

auto element_constant(
    const ProgramDraft& draft,
    std::optional<ConstantID> sequence,
    std::optional<ConstantID> index
) noexcept -> std::optional<ConstantID> {
    if (!sequence || !index) {
        return std::nullopt;
    }
    const auto* integer = std::get_if<IntegerConstant>(&draft.constant(*index).value);
    const auto position = integer ? integer->as_unsigned() : std::nullopt;
    const auto select = [&](const auto& value) noexcept -> std::optional<ConstantID> {
        if (!position || *position >= value.elements.size()) {
            return std::nullopt;
        }
        return value.elements[static_cast<std::size_t>(*position)];
    };
    const auto& fact = draft.constant(*sequence);
    if (const auto* array = std::get_if<ArrayConstant>(&fact.value)) {
        return select(*array);
    }
    if (const auto* slice = std::get_if<SliceConstant>(&fact.value)) {
        return select(*slice);
    }
    return std::nullopt;
}

// Optional normal-completion facts never execute a static root.
auto fold_constant_expression(ProgramDraft& draft, SemanticExpression& source) noexcept
    -> AnalysisResult<void> {
    const auto* type = std::get_if<TypeID>(&source.type.construction());
    if (!type) {
        return {};
    }
    auto failure = std::optional<ConstantEvaluationDiagnostic>();
    auto folded = std::optional<ConstantFact>();
    const auto retain = [&](auto result) noexcept {
        if (result) {
            folded = std::move(*result);
        } else if (const auto diagnostic = constant_evaluation_diagnostic(result.error())) {
            failure = diagnostic;
        }
    };
    const auto children =
        [](const auto& expressions) static noexcept -> std::optional<std::vector<ConstantID>> {
        auto values = std::vector<ConstantID>();
        for (const auto& expression : expressions) {
            if (!expression.constant) {
                return std::nullopt;
            }
            values.push_back(*expression.constant);
        }
        return values;
    };
    source.value.visit([&](auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemUnary>) {
            retain(
                fold_unary_constant(draft, operation.operation, operation.operand->constant, *type)
            );
        } else if constexpr (std::same_as<Operation, SemBinary>) {
            retain(fold_binary_constant(
                draft,
                operation.operation,
                operation.left->constant,
                operation.right->constant,
                *type
            ));
        } else if constexpr (std::same_as<Operation, SemCast>) {
            retain(fold_cast_constant(draft, operation.kind, operation.operand->constant, *type));
        } else if constexpr (std::same_as<Operation, SemShortCircuit>) {
            const auto truth =
                [&](const SemanticExpression& expression) noexcept -> std::optional<bool> {
                if (expression.constant) {
                    const auto& fact = draft.constant(*expression.constant);
                    if (const auto* boolean = std::get_if<BooleanConstant>(&fact.value)) {
                        return boolean->value;
                    }
                }
                return std::nullopt;
            };
            if (const auto left = truth(*operation.left)) {
                const auto selected = *left == (operation.operation == ShortCircuitOperator::And)
                    ? truth(*operation.right)
                    : left;
                if (selected) {
                    folded =
                        ConstantFact {.type = *type, .value = BooleanConstant {.value = *selected}};
                }
            }
        } else if constexpr (std::same_as<Operation, SemRange>) {
            if (operation.begin->constant && operation.end->constant) {
                const auto& first = draft.constant(*operation.begin->constant);
                const auto& last = draft.constant(*operation.end->constant);
                folded = ConstantFact {
                    .type = *type,
                    .value = RangeConstant {
                        .begin = std::get<IntegerConstant>(first.value),
                        .end = std::get<IntegerConstant>(last.value),
                        .inclusive = operation.inclusive,
                    }
                };
            }
        } else if constexpr (std::same_as<Operation, SemArray>) {
            source.constant = array_constant(draft, *type, operation);
        } else if constexpr (std::same_as<Operation, SemStruct>) {
            source.constant = struct_constant(draft, *type, operation);
        } else if constexpr (std::same_as<Operation, SemEnumCase>) {
            const auto declaration =
                draft.construction_enum_case_declaration_copy(operation.enum_case);
            if (declaration.constant) {
                source.constant = declaration.constant;
            } else if (auto values = children(operation.payload)) {
                folded = ConstantFact {
                    .type = *type,
                    .value = PayloadEnumConstant {
                        .enum_case = operation.enum_case,
                        .payload = std::move(*values)
                    },
                };
            }
        } else if constexpr (std::same_as<Operation, SemField>) {
            source.constant =
                field_constant(draft, operation.source->constant, operation.field.field_index);
        } else if constexpr (std::same_as<Operation, SemIndex>) {
            source.constant =
                element_constant(draft, operation.source->constant, operation.index->constant);
        } else if constexpr (std::same_as<Operation, SemIntrinsic>) {
            if (operation.operands.empty() || !operation.operands.front().expression.constant) {
                return;
            }
            const auto receiver = draft.constant(*operation.operands.front().expression.constant);
            operation.operation.visit([&](auto& intrinsic) noexcept {
                using Intrinsic = std::remove_cvref_t<decltype(intrinsic)>;
                if constexpr (std::same_as<Intrinsic, TextIntrinsic>) {
                    retain(
                        evaluate_text_intrinsic_constant_value(draft, intrinsic, receiver, *type)
                    );
                } else if constexpr (std::same_as<Intrinsic, SliceIntrinsicOperation>) {
                    if (intrinsic.intrinsic == SliceIntrinsic::FromArray) {
                        if (const auto* array = std::get_if<ArrayConstant>(&receiver.value)) {
                            folded = ConstantFact {
                                .type = *type,
                                .value = SliceConstant {.elements = array->elements},
                            };
                        }
                        return;
                    }
                    auto bounds = std::vector<ConstantFact>();
                    for (auto index = 1uz; index < operation.operands.size(); ++index) {
                        if (!operation.operands[index].expression.constant) {
                            return;
                        }
                        bounds.push_back(
                            draft.constant(*operation.operands[index].expression.constant)
                        );
                    }
                    retain(evaluate_slice_intrinsic_constant_value(
                        draft,
                        intrinsic.intrinsic,
                        receiver,
                        bounds,
                        *type
                    ));
                } else if constexpr (std::same_as<Intrinsic, SIMDIntrinsic>) {
                    const auto* operand_type = std::get_if<TypeID>(
                        &operation.operands.front().expression.type.construction()
                    );
                    if (!operand_type) {
                        return;
                    }
                    const auto owner =
                        simd_owner(intrinsic, *type, *operand_type, [&](TypeID id) noexcept {
                            return draft.type_copy(id);
                        });
                    auto operands = std::vector<std::optional<ConstantID>>();
                    for (const auto& operand : operation.operands) {
                        operands.push_back(operand.expression.constant);
                    }
                    retain(fold_simd_constant(draft, intrinsic, owner, operands, *type));
                }
            });
        }
    });
    if (failure) {
        return std::unexpected(draft.diagnostics().error(
            DiagnosticBuilder(failure->code, std::string(failure->message))
                .primary(draft.source_span(source.origin))
                .build()
        ));
    }
    if (folded) {
        source.constant = draft.intern_constant(std::move(*folded));
    }
    return {};
}
