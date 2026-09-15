module carven:semantic.evaluation.expr.impl;

import :semantic.evaluation.executor;
import std;

auto SemanticExecutor::value(ExecutionFrame& frame, const SemanticExpression& source) noexcept
    -> ExecutionResult<ExecutionValue> {
    auto result = expression(frame, source);
    if (!result) {
        return std::unexpected(result.error());
    }
    if (result->flow != ExecutionFlow::Normal) {
        return std::unexpected(fail(
            source.origin,
            DiagnosticCode::ConstEvaluation,
            "control transfer escaped a operand"
        ));
    }
    return std::move(result->value);
}

auto SemanticExecutor::expression(ExecutionFrame& frame, const SemanticExpression& source) noexcept
    -> ExecutionResult<ExecutionCompletion> {
    if (auto checked = step(source.origin); !checked) {
        return std::unexpected(checked.error());
    }
    if (const auto* conditional = std::get_if<SemIf>(&source.value)) {
        for (const auto& branch : conditional->branches) {
            auto condition = value(frame, branch.condition);
            if (!condition) {
                return std::unexpected(condition.error());
            }
            auto truth = boolean(*condition, branch.condition.origin);
            if (!truth) {
                return std::unexpected(truth.error());
            }
            if (*truth) {
                return region(frame, branch.body);
            }
        }
        return conditional->otherwise
            ? region(frame, **conditional->otherwise)
            : ExecutionResult<ExecutionCompletion>(
                  ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}}
              );
    }
    if (const auto* match = std::get_if<SemMatch>(&source.value)) {
        auto selected = std::optional<ExecutionPlace>();
        if (match->subject_is_place) {
            auto target = place(frame, *match->subject);
            if (!target) {
                return std::unexpected(target.error());
            }
            selected = std::move(*target);
        }
        auto subject = selected ? ExecutionResult<ExecutionValue>(ExecutionVoid {})
                                : value(frame, *match->subject);
        if (!subject) {
            return std::unexpected(subject.error());
        }
        for (const auto& arm : match->arms) {
            if (!arm.reachable) {
                continue;
            }
            if (selected) {
                auto target = located(frame, *selected, match->subject->origin);
                if (!target) {
                    return std::unexpected(target.error());
                }
                subject = copy_value(**target, match->subject->origin);
                if (!subject) {
                    return std::unexpected(subject.error());
                }
            }
            auto accepted = matches(frame, arm.pattern, *subject);
            if (!accepted) {
                return std::unexpected(accepted.error());
            }
            if (!*accepted) {
                continue;
            }
            if (arm.guard) {
                auto guard = value(frame, *arm.guard);
                if (!guard) {
                    return std::unexpected(guard.error());
                }
                auto truth = boolean(*guard, arm.guard->origin);
                if (!truth) {
                    return std::unexpected(truth.error());
                }
                if (!*truth) {
                    continue;
                }
            }
            return region(frame, arm.body);
        }
        return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
    }
    auto result = std::visit(
        [&](const auto& operation) noexcept -> ExecutionResult<ExecutionValue> {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, SemConstant>) {
                return ExecutionValue(operation.constant);
            } else if constexpr (std::same_as<Operation, SemBinding>) {
                auto local = slot_value(frame, operation.binding.index(), source.origin);
                if (!local) {
                    return std::unexpected(local.error());
                }
                return copy_value(**local, source.origin);
            } else if constexpr (std::same_as<Operation, SemTake>) {
                auto selected = place(frame, *operation.place);
                if (!selected) {
                    return std::unexpected(selected.error());
                }
                auto local = located(frame, *selected, source.origin);
                if (!local) {
                    return std::unexpected(local.error());
                }
                if (!selected->path.empty()) {
                    return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "take requires a whole binding"
                    ));
                }
                auto result = std::move(**local);
                frame.slots[selected->slot] = ExecutionTaken {};
                return result;
            } else if constexpr (std::same_as<Operation, SemStruct>) {
                auto result_type = type(source.type.construction(), source.origin);
                if (!result_type) {
                    return std::unexpected(result_type.error());
                }
                if (auto checked = check_aggregate_size(*result_type, source.origin); !checked) {
                    return std::unexpected(checked.error());
                }
                if (auto checked = account_aggregate(operation.fields.size(), source.origin);
                    !checked) {
                    return std::unexpected(checked.error());
                }
                auto elements =
                    std::vector<ExecutionValue>(operation.fields.size(), ExecutionVoid {});
                for (const auto& field : operation.fields) {
                    auto evaluated = value(frame, field.value);
                    if (!evaluated) {
                        return std::unexpected(evaluated.error());
                    }
                    elements.at(field.declaration_index) = std::move(*evaluated);
                }
                return ExecutionAggregateValue {
                    .type = *result_type,
                    .elements = std::move(elements)
                };
            } else if constexpr (std::same_as<Operation, SemField>) {
                if (local_place(source)) {
                    auto selected = place(frame, source);
                    if (!selected) {
                        return std::unexpected(selected.error());
                    }
                    auto target = located(frame, *selected, source.origin);
                    if (!target) {
                        return std::unexpected(target.error());
                    }
                    return copy_value(**target, source.origin);
                }
                auto receiver = value(frame, *operation.source);
                if (!receiver) {
                    return std::unexpected(receiver.error());
                }
                if (const auto* retained = std::get_if<ConstantID>(&*receiver)) {
                    const auto& fact = values.constant(*retained);
                    if (const auto* fields = std::get_if<StructConstant>(&fact.value)) {
                        return ExecutionValue(fields->fields.at(operation.field.field_index));
                    }
                }
                auto* aggregate = std::get_if<ExecutionAggregateValue>(&*receiver);
                if (aggregate == nullptr
                    || operation.field.field_index >= aggregate->elements.size()) {
                    return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "field requires a structure value"
                    ));
                }
                return std::move(aggregate->elements[operation.field.field_index]);
            } else if constexpr (std::same_as<Operation, SemEnumCase>) {
                auto target = type(source.type.construction(), source.origin);
                if (!target) {
                    return std::unexpected(target.error());
                }
                auto elements = std::vector<ExecutionValue>();
                for (const auto& child : operation.payload) {
                    auto evaluated = value(frame, child);
                    if (!evaluated) {
                        return std::unexpected(evaluated.error());
                    }
                    elements.push_back(std::move(*evaluated));
                }
                return ExecutionEnumValue {
                    .type = *target,
                    .enum_case = operation.enum_case,
                    .payload = std::move(elements)
                };
            } else if constexpr (std::same_as<Operation, SemSliceIntrinsic>) {
                auto receiver = value(frame, operation.operands[0].expression);
                if (!receiver) {
                    return std::unexpected(receiver.error());
                }
                const auto sequence = execution_compound_view(values, *receiver);
                if (!sequence) {
                    return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "slice requires a sequence"
                    ));
                }
                auto target = type(source.type.construction(), source.origin);
                if (!target) {
                    return std::unexpected(target.error());
                }
                switch (operation.intrinsic) {
                    case SliceIntrinsic::Len:
                        return ConstantAtom {
                            .type = *target,
                            .value = IntegerConstant::from_parts(sequence->size(), false)
                        };
                    case SliceIntrinsic::IsEmpty:
                        return ConstantAtom {
                            .type = *target,
                            .value = BooleanConstant {.value = sequence->size() == 0uz}
                        };
                    case SliceIntrinsic::FromArray:
                    case SliceIntrinsic::Slice:     {
                        auto begin = 0uz;
                        auto end = sequence->size();
                        if (operation.intrinsic == SliceIntrinsic::Slice) {
                            auto bounds = std::vector<std::uint64_t>();
                            for (auto index = 1uz; index < operation.operands.size(); ++index) {
                                auto operand = value(frame, operation.operands[index].expression);
                                if (!operand) {
                                    return std::unexpected(operand.error());
                                }
                                auto fact = read_fact(*operand, source.origin);
                                const auto* integer =
                                    fact ? std::get_if<IntegerConstant>(&fact->value) : nullptr;
                                if (!integer
                                    || integer->negative()
                                    || integer->magnitude() > sequence->size()) {
                                    return std::unexpected(fail(
                                        source.origin,
                                        DiagnosticCode::ConstIndexBounds,
                                        "slice range is out of bounds"
                                    ));
                                }
                                bounds.push_back(integer->magnitude());
                            }
                            if (bounds.size() != 2 || bounds[0] > bounds[1]) {
                                return std::unexpected(fail(
                                    source.origin,
                                    DiagnosticCode::ConstIndexBounds,
                                    "slice range is out of bounds"
                                ));
                            }
                            begin = static_cast<std::size_t>(bounds[0]);
                            end = static_cast<std::size_t>(bounds[1]);
                        }
                        if (auto checked = account_aggregate(end - begin, source.origin);
                            !checked) {
                            return std::unexpected(checked.error());
                        }
                        auto* aggregate = std::get_if<ExecutionAggregateValue>(&*receiver);
                        if (aggregate && operation.intrinsic == SliceIntrinsic::FromArray) {
                            aggregate->type = *target;
                            return std::move(*receiver);
                        }
                        auto elements = std::vector<ExecutionValue>();
                        elements.reserve(end - begin);
                        if (const auto* retained =
                                std::get_if<std::span<const ConstantID>>(&sequence->elements)) {
                            for (const auto child : retained->subspan(begin, end - begin)) {
                                elements.emplace_back(child);
                            }
                        } else {
                            for (auto index = begin; index < end; ++index) {
                                elements.push_back(std::move(execution_elements(*receiver)[index]));
                            }
                        }
                        return ExecutionAggregateValue {
                            .type = *target,
                            .elements = std::move(elements)
                        };
                    }
                }
                std::unreachable();
            } else if constexpr (std::same_as<Operation, SemArray>) {
                auto result_type = type(source.type.construction(), source.origin);
                if (!result_type) {
                    return std::unexpected(result_type.error());
                }
                if (auto checked = check_aggregate_size(*result_type, source.origin); !checked) {
                    return std::unexpected(checked.error());
                }
                if (auto checked = account_aggregate(operation.elements.size(), source.origin);
                    !checked) {
                    return std::unexpected(checked.error());
                }
                auto elements = std::vector<ExecutionValue>();
                elements.reserve(operation.elements.size());
                for (const auto& element : operation.elements) {
                    auto evaluated = value(frame, element);
                    if (!evaluated) {
                        return std::unexpected(evaluated.error());
                    }
                    elements.push_back(std::move(*evaluated));
                }
                return ExecutionAggregateValue {
                    .type = *result_type,
                    .elements = std::move(elements),
                };
            } else if constexpr (std::same_as<Operation, SemIndex>) {
                if (local_place(source)) {
                    auto selected = place(frame, source);
                    if (!selected) {
                        return std::unexpected(selected.error());
                    }
                    auto target = located(frame, *selected, source.origin);
                    if (!target) {
                        return std::unexpected(target.error());
                    }
                    return copy_value(**target, source.origin);
                }
                auto receiver = value(frame, *operation.source);
                if (!receiver) {
                    return std::unexpected(receiver.error());
                }
                auto subscript = value(frame, *operation.index);
                if (!subscript) {
                    return std::unexpected(subscript.error());
                }
                const auto sequence = execution_compound_view(values, *receiver);
                if (!sequence) {
                    return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "indexing requires an array value"
                    ));
                }
                auto position = offset(*subscript, sequence->size(), operation.index->origin);
                if (!position) {
                    return std::unexpected(position.error());
                }
                if (const auto* retained =
                        std::get_if<std::span<const ConstantID>>(&sequence->elements)) {
                    return ExecutionValue((*retained)[*position]);
                }
                return std::move(execution_elements(*receiver)[*position]);
            } else if constexpr (std::same_as<Operation, SemUnary>
                                 || std::same_as<Operation, SemCast>) {
                auto operand = value(frame, *operation.operand);
                if (!operand) {
                    return std::unexpected(operand.error());
                }
                if constexpr (std::same_as<Operation, SemCast>) {
                    if (operation.kind == CastKind::Identity) {
                        return std::move(*operand);
                    }
                }
                auto fact = read_fact(*operand, source.origin);
                auto target = type(source.type.construction(), source.origin);
                if (!fact || !target) {
                    return std::unexpected(!fact ? fact.error() : target.error());
                }
                if constexpr (std::same_as<Operation, SemUnary>) {
                    return finish(
                        evaluate_unary_constant_value(
                            values,
                            operation.operation,
                            *fact,
                            *target,
                            context.arithmetic()
                        ),
                        source.origin
                    );
                } else {
                    return finish(
                        evaluate_cast_constant_value(values, operation.kind, *fact, *target),
                        source.origin
                    );
                }
            } else if constexpr (std::same_as<Operation, SemBinary>) {
                auto left = value(frame, *operation.left);
                if (!left) {
                    return std::unexpected(left.error());
                }
                auto right = value(frame, *operation.right);
                if (!right) {
                    return std::unexpected(right.error());
                }
                if (operation.operation == BinaryOperator::Equal
                    || operation.operation == BinaryOperator::NotEqual) {
                    auto target = type(source.type.construction(), source.origin);
                    if (!target) {
                        return std::unexpected(target.error());
                    }
                    const auto compared = equal(*left, *right, source.origin);
                    if (!compared) {
                        return std::unexpected(compared.error());
                    }
                    return ConstantAtom {
                        .type = *target,
                        .value = BooleanConstant {
                            .value = operation.operation == BinaryOperator::Equal ? *compared
                                                                                  : !*compared
                        },
                    };
                }
                auto lhs = read_fact(*left, source.origin);
                auto rhs = read_fact(*right, source.origin);
                auto target = type(source.type.construction(), source.origin);
                if (!lhs || !rhs || !target) {
                    return std::unexpected(
                        !lhs       ? lhs.error()
                            : !rhs ? rhs.error()
                                   : target.error()
                    );
                }
                return finish(
                    evaluate_binary_constant_value(
                        values,
                        operation.operation,
                        *lhs,
                        *rhs,
                        *target,
                        context.arithmetic()
                    ),
                    source.origin
                );
            } else if constexpr (std::same_as<Operation, SemShortCircuit>) {
                auto left = value(frame, *operation.left);
                if (!left) {
                    return std::unexpected(left.error());
                }
                auto truth = boolean(*left, operation.left->origin);
                if (!truth) {
                    return std::unexpected(truth.error());
                }
                const auto conjunction = operation.operation == ShortCircuitOperator::And;
                return *truth != conjunction ? std::move(*left) : value(frame, *operation.right);
            } else if constexpr (std::same_as<Operation, SemCall>) {
                const auto* callee = std::get_if<SemCallable>(&operation.callee->value);
                const auto function =
                    callee ? context.function_for_callable(callee->callable) : std::nullopt;
                if (!function) {
                    return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstAdmission,
                        "execution requires a direct function call"
                    ));
                }
                auto operands = std::vector<ExecutionOperand>();
                for (const auto& argument : operation.arguments) {
                    auto evaluated = read_operand(frame, argument.expression);
                    if (!evaluated) {
                        return std::unexpected(evaluated.error());
                    }
                    operands.push_back(std::move(*evaluated));
                }
                auto arguments = std::vector<ExecutionValue>();
                for (auto& operand : operands) {
                    auto evaluated = materialize(frame, std::move(operand), source.origin);
                    if (!evaluated) {
                        return std::unexpected(evaluated.error());
                    }
                    arguments.push_back(std::move(*evaluated));
                }
                return invoke(*function, std::move(arguments), source.origin);
            } else if constexpr (std::same_as<Operation, SemPrint>) {
                return print(frame, operation, source.origin);
            } else if constexpr (std::same_as<Operation, SemTestReport>) {
                return test_report(frame, operation, source.origin);
            } else if constexpr (std::same_as<Operation, SemFormat>) {
                return format(frame, operation, source.origin);
            } else if constexpr (std::same_as<Operation, SemTextIntrinsic>) {
                auto target = type(source.type.construction(), source.origin);
                if (!target) {
                    return std::unexpected(target.error());
                }
                return text_intrinsic(frame, operation, *target, source.origin);
            } else {
                return std::unexpected(fail(
                    source.origin,
                    DiagnosticCode::ConstEvaluation,
                    "operation is not supported in execution"
                ));
            }
        },
        source.value
    );
    if (!result) {
        return std::unexpected(result.error());
    }
    return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = std::move(*result)};
}

auto SemanticExecutor::matches(
    ExecutionFrame& frame,
    PatternID id,
    const ExecutionValue& subject
) noexcept -> ExecutionResult<bool> {
    return frame.body->visit_pattern(
        id,
        [&](const auto& pattern) noexcept -> ExecutionResult<bool> {
            if (auto checked = step(pattern.origin); !checked) {
                return std::unexpected(checked.error());
            }
            return std::visit(
                [&](const auto& pattern_value) noexcept -> ExecutionResult<bool> {
                    using Pattern = std::remove_cvref_t<decltype(pattern_value)>;
                    if constexpr (std::same_as<Pattern, WildcardPattern>) {
                        return true;
                    } else if constexpr (std::same_as<Pattern, BindingPattern>) {
                        auto copied = copy_value(subject, pattern.origin);
                        if (!copied) {
                            return std::unexpected(copied.error());
                        }
                        frame.slots[pattern_value.binding.index()] = std::move(*copied);
                        return true;
                    } else if constexpr (std::same_as<Pattern, LiteralPattern>) {
                        return equal(
                            subject,
                            ExecutionValue(pattern_value.constant),
                            pattern.origin
                        );
                    } else if constexpr (std::same_as<Pattern, OrPattern>) {
                        for (const auto alternative : pattern_value.alternatives) {
                            auto accepted = matches(frame, alternative, subject);
                            if (!accepted || *accepted) {
                                return accepted;
                            }
                        }
                        return false;
                    } else {
                        return std::unexpected(fail(
                            pattern.origin,
                            DiagnosticCode::ConstEvaluation,
                            "pattern is not supported in execution"
                        ));
                    }
                },
                pattern.value
            );
        }
    );
}
