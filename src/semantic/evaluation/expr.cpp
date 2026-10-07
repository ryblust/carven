module carven:semantic.evaluation.expr.impl;

import :semantic.evaluation.admission;
import :semantic.evaluation.executor;
import :semantic.semir.children;
import :semantic.semir.delegation;
import :semantic.semir.simd;
import :support.invariant;
import std;

auto SemanticExecutor::finish_unary_value(
    const SemanticExpression& source,
    UnaryOperator operation,
    const ExecutionValue& operand
) noexcept -> ExecutionResult<ExecutionValue> {
    auto fact = read_fact(operand, source.origin);
    auto target = type(source.type.construction(), source.origin);
    if (!fact || !target) {
        return std::unexpected(!fact ? std::move(fact.error()) : std::move(target.error()));
    }
    return finish(evaluate_unary_constant_value(values, operation, *fact, *target), source.origin);
}

auto SemanticExecutor::finish_cast_value(
    const SemanticExpression& source,
    CastKind kind,
    ExecutionValue&& operand
) noexcept -> ExecutionResult<ExecutionValue> {
    if (kind == CastKind::Identity) {
        return std::move(operand);
    }
    if (kind == CastKind::PointerRead) {
        auto target = type(source.type.construction(), source.origin);
        if (!target) {
            return std::unexpected(std::move(target.error()));
        }
        if (auto* pointer = std::get_if<ExecutionPointer>(&operand)) {
            pointer->type = *target;
            return std::move(operand);
        }
        return ConstantAtom {.type = *target, .value = NullPointerConstant {}};
    }
    auto fact = read_fact(operand, source.origin);
    auto target = type(source.type.construction(), source.origin);
    if (!fact || !target) {
        return std::unexpected(!fact ? std::move(fact.error()) : std::move(target.error()));
    }
    return finish(evaluate_cast_constant_value(values, kind, *fact, *target), source.origin);
}

auto SemanticExecutor::finish_binary_value(
    const SemanticExpression& source,
    const SemBinary& operation,
    const ExecutionValue& left,
    const ExecutionValue& right
) noexcept -> ExecutionResult<ExecutionValue> {
    // Vector equality is lane-wise and yields a mask; it uses the operator path.
    const auto vector = [&]() noexcept {
        const auto* operand = std::get_if<TypeID>(&operation.left->type.construction());
        if (!operand) {
            return false;
        }
        const auto canonical = values.type_copy(*operand);
        const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
        return builtin != nullptr && simd_layout(builtin->kind).has_value();
    }();
    if ((operation.operation == BinaryOperator::Equal
         || operation.operation == BinaryOperator::NotEqual)
        && !vector) {
        auto target = type(source.type.construction(), source.origin);
        if (!target) {
            return std::unexpected(std::move(target.error()));
        }
        auto compared = equal(left, right, source.origin);
        if (!compared) {
            return std::unexpected(std::move(compared.error()));
        }
        observe_condition(
            source,
            left,
            &right,
            operation.operation == BinaryOperator::Equal ? *compared : !*compared
        );
        return ConstantAtom {
            .type = *target,
            .value = BooleanConstant {
                .value = operation.operation == BinaryOperator::Equal ? *compared : !*compared
            },
        };
    }
    auto lhs = read_fact(left, source.origin);
    auto rhs = read_fact(right, source.origin);
    auto target = type(source.type.construction(), source.origin);
    if (!lhs || !rhs || !target) {
        return std::unexpected(
            !lhs       ? std::move(lhs.error())
                : !rhs ? std::move(rhs.error())
                       : std::move(target.error())
        );
    }
    auto compared = finish(
        evaluate_binary_constant_value(values, operation.operation, *lhs, *rhs, *target),
        source.origin
    );
    if (compared
        && current->condition_observation
        && current->condition_observation->condition == &source) {
        auto truth = boolean(*compared, source.origin);
        if (truth) {
            observe_condition(source, left, &right, *truth);
        }
    }
    return compared;
}

auto SemanticExecutor::try_expression(ExecutionFrame& frame, const SemTry& attempt) noexcept
    -> ExecutionTask<ExecutionCompletion> {
    auto protected_result = (co_await region(frame, *attempt.body));
    if (protected_result) {
        co_return protected_result;
    }
    const auto* source_failure = std::get_if<ExecutionSourceFailure>(&protected_result.error());
    if (source_failure == nullptr) {
        co_return protected_result;
    }
    // The original payload remains independent of copied bindings and nested catches.
    const auto failure = *source_failure;
    for (const auto& arm : attempt.arms) {
        const auto reset = [&]() noexcept {
            for (const auto binding : arm.bindings) {
                release(frame, binding.index());
            }
        };
        auto matched = false;
        for (const auto& alternative : arm.alternatives) {
            if (!alternative.reachable) {
                continue;
            }
            reset();
            const auto pattern_temporaries = frame.temporaries.size();
            if (std::holds_alternative<CatchAllPattern>(alternative.pattern)) {
                matched = true;
            } else if (const auto* typed =
                           std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                auto catch_type = type(typed->type.construction(), alternative.origin);
                if (!catch_type) {
                    co_return std::unexpected(std::move(catch_type.error()));
                }
                if (*catch_type != failure.type) {
                    continue;
                }
                auto accepted =
                    (co_await matches(frame, typed->inner, *failure.payload, arm.pattern_bounds));
                if (!accepted) {
                    release_temporaries(frame, pattern_temporaries);
                    reset();
                    co_return std::unexpected(std::move(accepted.error()));
                }
                matched = *accepted;
            }
            release_temporaries(frame, pattern_temporaries);
            if (matched) {
                break;
            }
        }
        if (!matched) {
            reset();
            continue;
        }
        frame.caught.push_back(failure);
        auto recover =
            co_await [&]() noexcept -> ExecutionTask<std::optional<ExecutionCompletion>> {
            if (arm.guard) {
                const auto temporaries = frame.temporaries.size();
                auto guard = (co_await this->value(frame, *arm.guard));
                if (!guard) {
                    release_temporaries(frame, temporaries);
                    co_return std::unexpected(std::move(guard.error()));
                }
                auto truth = boolean(*guard, arm.guard->origin);
                release_temporaries(frame, temporaries);
                if (!truth) {
                    co_return std::unexpected(std::move(truth.error()));
                }
                if (!*truth) {
                    co_return std::nullopt;
                }
            }
            auto result = (co_await region(frame, arm.body));
            if (!result) {
                co_return std::unexpected(std::move(result.error()));
            }
            co_return std::move(*result);
        }();
        frame.caught.pop_back();
        reset();
        if (!recover) {
            co_return std::unexpected(std::move(recover.error()));
        }
        if (*recover) {
            co_return std::move(**recover);
        }
    }
    co_return protected_result;
}

auto SemanticExecutor::if_expression(ExecutionFrame& frame, const SemIf& conditional) noexcept
    -> ExecutionTask<ExecutionCompletion> {
    for (const auto& branch : conditional.branches) {
        const auto temporaries = frame.temporaries.size();
        auto condition = (co_await this->value(frame, branch.condition));
        if (!condition) {
            release_temporaries(frame, temporaries);
            co_return std::unexpected(std::move(condition.error()));
        }
        auto truth = boolean(*condition, branch.condition.origin);
        release_temporaries(frame, temporaries);
        if (!truth) {
            co_return std::unexpected(std::move(truth.error()));
        }
        if (*truth) {
            co_return (co_await region(frame, branch.body));
        }
    }
    co_return conditional.otherwise
        ? (co_await region(frame, **conditional.otherwise))
        : ExecutionResult<ExecutionCompletion>(
              ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}}
          );
}

auto SemanticExecutor::match_expression(ExecutionFrame& frame, const SemMatch& match) noexcept
    -> ExecutionTask<ExecutionCompletion> {
    auto selected = std::optional<ExecutionPlace>();
    if (match.subject_is_place) {
        auto target = (co_await place(frame, *match.subject));
        if (!target) {
            co_return std::unexpected(std::move(target.error()));
        }
        selected = std::move(*target);
    }
    auto subject = selected ? ExecutionResult<ExecutionValue>(ExecutionVoid {})
                            : (co_await this->value(frame, *match.subject));
    if (!subject) {
        co_return std::unexpected(std::move(subject.error()));
    }
    for (const auto& arm : match.arms) {
        if (!arm.reachable) {
            continue;
        }
        const auto reset = [&]() noexcept {
            for (const auto binding : arm.bindings) {
                release(frame, binding.index());
            }
        };
        reset();
        if (selected) {
            auto target = located(*selected, match.subject->origin);
            if (!target) {
                co_return std::unexpected(std::move(target.error()));
            }
            subject = copy_value(**target, match.subject->origin);
            if (!subject) {
                co_return std::unexpected(std::move(subject.error()));
            }
        }
        const auto pattern_temporaries = frame.temporaries.size();
        auto accepted = (co_await matches(frame, arm.pattern, *subject, arm.pattern_bounds));
        release_temporaries(frame, pattern_temporaries);
        if (!accepted) {
            reset();
            co_return std::unexpected(std::move(accepted.error()));
        }
        if (!*accepted) {
            reset();
            continue;
        }
        if (arm.guard) {
            const auto guard_temporaries = frame.temporaries.size();
            auto guard = (co_await this->value(frame, *arm.guard));
            if (!guard) {
                release_temporaries(frame, guard_temporaries);
                reset();
                co_return std::unexpected(std::move(guard.error()));
            }
            auto truth = boolean(*guard, arm.guard->origin);
            release_temporaries(frame, guard_temporaries);
            if (!truth) {
                reset();
                co_return std::unexpected(std::move(truth.error()));
            }
            if (!*truth) {
                reset();
                continue;
            }
        }
        auto result = (co_await region(frame, arm.body));
        reset();
        co_return result;
    }
    co_return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
}

auto SemanticExecutor::unsupported_expression(
    ExecutionFrame& frame,
    const SemanticExpression& source,
    std::string_view reason
) noexcept -> ExecutionTask<ExecutionCompletion> {
    struct Input final {
        const SemanticExpression* expression;
        OperandUse use;
    };

    auto inputs = std::vector<Input>();
    const auto native_input = [&](AccessMode access,
                                  const SemanticExpression& expression) noexcept {
        const auto use = access == AccessMode::Read
                || (access == AccessMode::Take && expression.selects_storage())
            ? OperandUse::Borrow
            : argument_use(access);
        inputs.push_back({.expression = &expression, .use = use});
    };
    source.value.visit([&](const auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemCpp>) {
            const auto* conversion = std::get_if<CppConvertOperation>(&operation.operation);
            if (conversion != nullptr
                && !conversion->explicit_cast
                && source.category == SemanticValueCategory::Value) {
                // Value initialization consumes its input, independently of native Read access.
                inputs.push_back({
                    .expression = &operation.operands.front().expression,
                    .use = OperandUse::Value,
                });
            } else {
                visit_cpp_operands(operation, native_input);
            }
        } else if constexpr (std::same_as<Operation, SemCppCall>) {
            visit_cpp_operands(operation, native_input);
        } else if constexpr (std::same_as<Operation, SemIntrinsic>) {
            for (const auto& input : operation.operands) {
                inputs.push_back({
                    .expression = &input.expression,
                    .use = argument_use(input.access),
                });
            }
        } else if constexpr (std::same_as<Operation, SemClosure>) {
            for (const auto& capture : operation.captures) {
                inputs.push_back({
                    .expression = &capture.expression,
                    .use =
                        capture.mode == CaptureMode::Write ? OperandUse::Write : OperandUse::Value,
                });
            }
        } else if constexpr (std::same_as<Operation, SemArrayAdopt>) {
            inputs.push_back({.expression = &*operation.source, .use = OperandUse::Borrow});
        } else {
            visit_semantic_children(operation, [&](const auto& child) noexcept {
                using Child = std::remove_cvref_t<decltype(child)>;
                if constexpr (std::same_as<Child, SemanticExpression>) {
                    inputs.push_back({.expression = &child, .use = OperandUse::Value});
                }
            });
        }
    });
    auto operands = std::vector<ExecutionOperand>();
    for (const auto& input : inputs) {
        auto result = (co_await operand(frame, *input.expression, input.use, source.origin));
        if (!result) {
            co_return std::unexpected(std::move(result.error()));
        }
        operands.push_back(std::move(*result));
    }
    co_return std::unexpected(fail(source.origin, ExecutionReason::Admission, std::string(reason)));
}

template<typename Operation>
auto SemanticExecutor::expression_value(
    ExecutionFrame& frame,
    const SemanticExpression& source,
    const Operation& operation
) noexcept -> ExecutionTask<ExecutionValue> {
    if constexpr (std::same_as<Operation, SemDefault>) {
        auto target = type(source.type.construction(), source.origin);
        if (!target) {
            co_return std::unexpected(std::move(target.error()));
        }
        co_return (co_await default_value(*target, source.origin));
    } else if constexpr (std::same_as<Operation, SemCallable>) {
        co_return ExecutionFunction {
            .type = source.type.construction(),
            .callable = operation.callable
        };
    } else if constexpr (std::same_as<Operation, SemBorrowCallable>) {
        auto function = (co_await this->value(frame, *operation.source));
        if (!function) {
            co_return std::unexpected(std::move(function.error()));
        }
        if (auto* target = std::get_if<ExecutionFunction>(&*function)) {
            target->type = source.type.construction();
            co_return std::move(*function);
        }
        co_return std::unexpected(fail(
            source.origin,
            ExecutionReason::Evaluation,
            "callable is not supported in execution"
        ));
    } else if constexpr (std::same_as<Operation, SemAddressOf>) {
        auto selected = (co_await place(frame, *operation.source));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        if (auto target = located(*selected, source.origin); !target) {
            co_return std::unexpected(std::move(target.error()));
        }
        auto pointer_type = type(source.type.construction(), source.origin);
        if (!pointer_type) {
            co_return std::unexpected(std::move(pointer_type.error()));
        }
        co_return ExecutionPointer {.type = *pointer_type, .target = std::move(*selected)};
    } else if constexpr (std::same_as<Operation, SemDereference>) {
        auto selected = (co_await place(frame, source));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        auto target = located(*selected, source.origin);
        if (!target) {
            co_return std::unexpected(std::move(target.error()));
        }
        co_return copy_value(**target, source.origin);
    } else if constexpr (std::same_as<Operation, SemTake>) {
        auto selected = (co_await place(frame, *operation.place));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        auto local = located(*selected, source.origin);
        if (!local) {
            co_return std::unexpected(std::move(local.error()));
        }
        if (!selected->path.empty()) {
            co_return std::unexpected(
                fail(source.origin, ExecutionReason::Evaluation, "take requires a whole binding")
            );
        }
        transfer_owned_text(**local);
        auto result = std::move(**local);
        const auto* binding = std::get_if<SemBinding>(&operation.place->value);
        if (binding == nullptr) {
            co_return std::unexpected(
                fail(source.origin, ExecutionReason::Evaluation, "take requires an owned binding")
            );
        }
        release(frame, binding->binding.index());
        frame.slots[binding->binding.index()] = ExecutionTaken {};
        co_return result;
    } else if constexpr (std::same_as<Operation, SemStruct>) {
        auto result_type = type(source.type.construction(), source.origin);
        if (!result_type) {
            co_return std::unexpected(std::move(result_type.error()));
        }
        if (auto checked = check_aggregate_size(*result_type, source.origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (auto checked = account_aggregate(operation.fields.size(), source.origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        auto elements = std::vector<ExecutionValue>();
        elements.reserve(operation.fields.size());
        for (auto index = 0uz; index < operation.fields.size(); ++index) {
            elements.emplace_back(ExecutionVoid {});
        }
        for (const auto& field : operation.fields) {
            auto evaluated = (co_await this->value(frame, field.value));
            if (!evaluated) {
                co_return std::unexpected(std::move(evaluated.error()));
            }
            elements.at(field.declaration_index) = std::move(*evaluated);
        }
        co_return ExecutionAggregateValue {.type = *result_type, .elements = std::move(elements)};
    } else if constexpr (std::same_as<Operation, SemField>) {
        if (has_bound_storage(source)) {
            auto selected = (co_await place(frame, source));
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            auto target = located(*selected, source.origin);
            if (!target) {
                co_return std::unexpected(std::move(target.error()));
            }
            co_return copy_value(**target, source.origin);
        }
        auto receiver = (co_await this->value(frame, *operation.source));
        if (!receiver) {
            co_return std::unexpected(std::move(receiver.error()));
        }
        if (const auto* retained = std::get_if<ConstantID>(&*receiver)) {
            const auto& fact = values.constant(*retained);
            if (const auto* fields = std::get_if<StructConstant>(&fact.value)) {
                co_return ExecutionValue(fields->fields.at(operation.field.field_index));
            }
        }
        auto* aggregate = std::get_if<ExecutionAggregateValue>(&*receiver);
        if (aggregate == nullptr || operation.field.field_index >= aggregate->elements.size()) {
            co_return std::unexpected(
                fail(source.origin, ExecutionReason::Evaluation, "field requires a structure value")
            );
        }
        co_return std::move(aggregate->elements[operation.field.field_index]);
    } else if constexpr (std::same_as<Operation, SemEnumCase>) {
        auto target = type(source.type.construction(), source.origin);
        if (!target) {
            co_return std::unexpected(std::move(target.error()));
        }
        if (auto checked = check_aggregate_size(*target, source.origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (auto checked = account_aggregate(operation.payload.size(), source.origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        auto elements = std::vector<ExecutionValue>();
        for (const auto& child : operation.payload) {
            auto evaluated = (co_await this->value(frame, child));
            if (!evaluated) {
                co_return std::unexpected(std::move(evaluated.error()));
            }
            elements.push_back(std::move(*evaluated));
        }
        co_return ExecutionEnumValue {
            .type = *target,
            .enum_case = operation.enum_case,
            .payload = std::move(elements)
        };
    } else if constexpr (std::same_as<Operation, SemIntrinsic>) {
        if (std::holds_alternative<SIMDIntrinsic>(operation.operation)) {
            co_return co_await simd(frame, operation, source);
        }
        if (std::holds_alternative<TextIntrinsic>(operation.operation)) {
            auto target = type(source.type.construction(), source.origin);
            if (!target) {
                co_return std::unexpected(std::move(target.error()));
            }
            co_return co_await text_intrinsic(frame, operation, *target, source.origin);
        }
        const auto intrinsic = std::get<SliceIntrinsicOperation>(operation.operation).intrinsic;
        auto sequence =
            (co_await sequence_view(frame, operation.operands[0].expression, source.origin));
        if (!sequence) {
            co_return std::unexpected(std::move(sequence.error()));
        }
        auto target = type(source.type.construction(), source.origin);
        if (!target) {
            co_return std::unexpected(std::move(target.error()));
        }
        switch (intrinsic) {
            case SliceIntrinsic::Len:
                co_return ConstantAtom {
                    .type = *target,
                    .value = IntegerConstant::from_parts(sequence->extent, false)
                };
            case SliceIntrinsic::IsEmpty:
                co_return ConstantAtom {
                    .type = *target,
                    .value = BooleanConstant {.value = sequence->extent == 0uz}
                };
            case SliceIntrinsic::FromArray:
            case SliceIntrinsic::Slice:     {
                auto begin = 0uz;
                auto end = sequence->extent;
                if (intrinsic == SliceIntrinsic::Slice) {
                    auto bounds = std::vector<std::uint64_t>();
                    for (auto index = 1uz; index < operation.operands.size(); ++index) {
                        auto operand =
                            (co_await this->value(frame, operation.operands[index].expression));
                        if (!operand) {
                            co_return std::unexpected(std::move(operand.error()));
                        }
                        auto fact = read_fact(*operand, source.origin);
                        if (!fact) {
                            co_return std::unexpected(std::move(fact.error()));
                        }
                        const auto* integer = std::get_if<IntegerConstant>(&fact->value);
                        if (!integer) {
                            co_return std::unexpected(fail(
                                source.origin,
                                ExecutionReason::Evaluation,
                                "slice bound must be an integer"
                            ));
                        }
                        if (integer->negative() || integer->magnitude() > sequence->extent) {
                            co_return std::unexpected(trap(
                                source.origin,
                                ExecutionReason::IndexBounds,
                                "slice range is out of bounds"
                            ));
                        }
                        bounds.push_back(integer->magnitude());
                    }
                    if (bounds.size() != 2) {
                        co_return std::unexpected(fail(
                            source.origin,
                            ExecutionReason::Evaluation,
                            "slice range requires two bounds"
                        ));
                    }
                    if (bounds[0] > bounds[1]) {
                        co_return std::unexpected(trap(
                            source.origin,
                            ExecutionReason::IndexBounds,
                            "slice range is out of bounds"
                        ));
                    }
                    begin = static_cast<std::size_t>(bounds[0]);
                    end = static_cast<std::size_t>(bounds[1]);
                }
                if (!memory.view(*sequence)) {
                    co_return std::unexpected(fail(
                        source.origin,
                        ExecutionReason::Evaluation,
                        "slice backing is no longer alive"
                    ));
                }
                co_return ExecutionSlice {
                    .type = *target,
                    .backing = std::move(sequence->backing),
                    .offset = sequence->offset + begin,
                    .extent = end - begin
                };
            }
        }
        std::unreachable();
    } else if constexpr (std::same_as<Operation, SemRange>) {
        auto begin = (co_await this->value(frame, *operation.begin));
        if (!begin) {
            co_return std::unexpected(std::move(begin.error()));
        }
        auto end = (co_await this->value(frame, *operation.end));
        if (!end) {
            co_return std::unexpected(std::move(end.error()));
        }
        auto first = read_fact(*begin, source.origin);
        auto last = read_fact(*end, source.origin);
        if (!first || !last) {
            co_return std::unexpected(!first ? std::move(first.error()) : std::move(last.error()));
        }
        auto range_type = type(source.type.construction(), source.origin);
        if (!range_type) {
            co_return std::unexpected(std::move(range_type.error()));
        }
        co_return ExecutionValue(
            ConstantAtom {
                .type = *range_type,
                .value = RangeConstant {
                    .begin = std::get<IntegerConstant>(first->value),
                    .end = std::get<IntegerConstant>(last->value),
                    .inclusive = operation.inclusive
                }
            }
        );
    } else if constexpr (std::same_as<Operation, SemArray>) {
        auto result_type = type(source.type.construction(), source.origin);
        if (!result_type) {
            co_return std::unexpected(std::move(result_type.error()));
        }
        if (auto checked = check_aggregate_size(*result_type, source.origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (auto checked = account_aggregate(operation.elements.size(), source.origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        auto elements = std::vector<ExecutionValue>();
        elements.reserve(operation.elements.size());
        for (const auto& element : operation.elements) {
            auto evaluated = (co_await this->value(frame, element));
            if (!evaluated) {
                co_return std::unexpected(std::move(evaluated.error()));
            }
            elements.push_back(std::move(*evaluated));
        }
        co_return ExecutionAggregateValue {
            .type = *result_type,
            .elements = std::move(elements),
        };
    } else if constexpr (std::same_as<Operation, SemIndex>) {
        if (has_bound_storage(source)) {
            auto selected = (co_await place(frame, source));
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            auto target = located(*selected, source.origin);
            if (!target) {
                co_return std::unexpected(std::move(target.error()));
            }
            co_return copy_value(**target, source.origin);
        }
        auto receiver = (co_await this->value(frame, *operation.source));
        if (!receiver) {
            co_return std::unexpected(std::move(receiver.error()));
        }
        auto subscript = (co_await this->value(frame, *operation.index));
        if (!subscript) {
            co_return std::unexpected(std::move(subscript.error()));
        }
        auto slice = std::optional<ExecutionSlice>();
        if (const auto* view = std::get_if<ExecutionSlice>(&*receiver)) {
            slice = *view;
        } else if (const auto* constant = std::get_if<ConstantID>(&*receiver); constant
                   && std::holds_alternative<SliceConstant>(values.constant(*constant).value)) {
            auto retained = retained_slice(*constant, source.origin);
            if (!retained) {
                co_return std::unexpected(std::move(retained.error()));
            }
            slice = std::move(*retained);
        }
        if (slice) {
            auto position = offset(*subscript, slice->extent, operation.index->origin);
            if (!position) {
                co_return std::unexpected(std::move(position.error()));
            }
            auto selected = slice_element(*slice, *position, source.origin);
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            auto element = located(*selected, source.origin);
            if (!element) {
                co_return std::unexpected(std::move(element.error()));
            }
            co_return copy_value(**element, source.origin);
        }
        const auto sequence = execution_compound_view(values, *receiver);
        if (!sequence) {
            co_return std::unexpected(
                fail(source.origin, ExecutionReason::Evaluation, "indexing requires an array value")
            );
        }
        auto position = offset(*subscript, sequence->size(), operation.index->origin);
        if (!position) {
            co_return std::unexpected(std::move(position.error()));
        }
        if (const auto* retained = std::get_if<std::span<const ConstantID>>(&sequence->elements)) {
            co_return ExecutionValue((*retained)[*position]);
        }
        co_return std::move(execution_elements(*receiver)[*position]);
    } else if constexpr (std::same_as<Operation, SemShortCircuit>) {
        auto left = (co_await this->value(frame, *operation.left));
        if (!left) {
            co_return std::unexpected(std::move(left.error()));
        }
        auto truth = boolean(*left, operation.left->origin);
        if (!truth) {
            co_return std::unexpected(std::move(truth.error()));
        }
        const auto conjunction = operation.operation == ShortCircuitOperator::And;
        if (*truth != conjunction) {
            observe_condition(source, *left, nullptr, *truth);
            co_return std::move(*left);
        }
        auto right = (co_await this->value(frame, *operation.right));
        if (right) {
            auto result = boolean(*right, source.origin);
            if (result) {
                observe_condition(source, *left, &*right, *result);
            }
        }
        co_return right;
    } else if constexpr (std::same_as<Operation, SemCall> || std::same_as<Operation, SemColdCall>) {
        auto evaluated_callee = (co_await this->value(frame, *operation.callee));
        if (!evaluated_callee) {
            co_return std::unexpected(std::move(evaluated_callee.error()));
        }
        const auto* callee = std::get_if<ExecutionFunction>(&*evaluated_callee);
        if (!callee) {
            co_return std::unexpected(fail(
                source.origin,
                ExecutionReason::Admission,
                "callable has no executable Carven body"
            ));
        }
        auto operands = std::vector<ExecutionOperand>();
        for (const auto& argument : operation.arguments) {
            auto evaluated = (co_await operand(
                frame,
                argument.expression,
                argument_use(argument.access),
                source.origin
            ));
            if (!evaluated) {
                co_return std::unexpected(std::move(evaluated.error()));
            }
            operands.push_back(std::move(*evaluated));
        }
        if constexpr (std::same_as<Operation, SemColdCall>) {
            auto selected =
                co_await context.bind_call(callee->callable, operands, *this, source.origin);
            if (!selected) {
                if (auto* event = std::get_if<ExecutionEvent>(&selected.error())) {
                    event->calls = current->calls;
                    event->blocks = current->blocks;
                    co_return std::unexpected(halt(std::move(*event)));
                }
                co_return std::unexpected(
                    stop(std::move(std::get<ExecutionFailure>(selected.error())))
                );
            }
            co_return std::make_unique<ExecutionColdOperation>(ExecutionColdOperation {
                .type = source.type.construction(),
                .action = *selected,
                .arguments = std::move(operands),
            });
        } else {
            co_return (co_await call(callee->callable, std::move(operands), source.origin));
        }
    } else if constexpr (std::same_as<Operation, SemAsyncIntrinsic>) {
        switch (operation.kind) {
            case AsyncIntrinsic::CancellationRequested: {
                auto result_type = type(source.type.construction(), source.origin);
                if (!result_type) {
                    co_return std::unexpected(std::move(result_type.error()));
                }
                co_return ConstantAtom {
                    .type = *result_type,
                    .value = BooleanConstant {.value = cancellation_requested()},
                };
            }
            case AsyncIntrinsic::CancelChild: {
                const auto* child =
                    std::get_if<ExecutionChild>(&frame.slots.at(operation.child->index()));
                if (child == nullptr || !child->task->operation) {
                    co_return std::unexpected(fail(
                        source.origin,
                        ExecutionReason::Evaluation,
                        "cancel requires an unconsumed lexical child"
                    ));
                }
                child->task->requested = true;
                co_return ExecutionVoid {};
            }
            case AsyncIntrinsic::YieldOnce:
            case AsyncIntrinsic::CancellationPoint:
                co_return std::make_unique<ExecutionColdOperation>(ExecutionColdOperation {
                    .type = source.type.construction(),
                    .action = operation.kind,
                    .arguments = {},
                });
        }
        std::unreachable();
    } else if constexpr (std::same_as<Operation, SemAwait>) {
        if (operation.operand_kind == AsyncAwaitOperandKind::LexicalChild) {
            const auto* binding = std::get_if<SemBinding>(&operation.operand->value);
            if (binding == nullptr) {
                invariant_violation("execution child observation lacks canonical binding");
            }
            co_return co_await observe_child(frame, binding->binding, source.origin);
        }
        auto evaluated = co_await this->value(frame, *operation.operand);
        if (!evaluated) {
            co_return std::unexpected(std::move(evaluated.error()));
        }
        auto* descriptor = std::get_if<std::unique_ptr<ExecutionColdOperation>>(&*evaluated);
        if (descriptor == nullptr || !*descriptor) {
            co_return std::unexpected(fail(
                source.origin,
                ExecutionReason::Evaluation,
                "await requires an unconsumed cold operation"
            ));
        }
        auto owned = std::move(*descriptor);
        co_return co_await consume(std::move(*owned), source.origin);
    } else if constexpr (std::same_as<Operation, SemPrint>) {
        co_return (co_await print(frame, operation, source.origin));
    } else if constexpr (std::same_as<Operation, SemReport>) {
        co_return (co_await report(frame, operation, source.origin));
    } else if constexpr (std::same_as<Operation, SemFormat>) {
        co_return (co_await format(frame, operation, source.origin));
    } else {
        co_return std::unexpected(fail(
            source.origin,
            ExecutionReason::Evaluation,
            "operation is not supported in execution"
        ));
    }
}

template<typename Result>
auto SemanticExecutor::normal_result(ExecutionResult<ExecutionValue>&& result) noexcept
    -> std::conditional_t<
        std::same_as<Result, ExecutionValue>,
        ExecutionResult<ExecutionValue>&&,
        ExecutionResult<Result>> {
    if constexpr (std::same_as<Result, ExecutionValue>) {
        return std::move(result);
    } else {
        static_assert(std::same_as<Result, ExecutionCompletion>);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = std::move(*result)};
    }
}

template<typename Result>
auto SemanticExecutor::control_result(
    ExecutionResult<ExecutionCompletion>&& result,
    ProgramOriginID origin
) noexcept
    -> std::conditional_t<
        std::same_as<Result, ExecutionCompletion>,
        ExecutionResult<ExecutionCompletion>&&,
        ExecutionResult<Result>> {
    if constexpr (std::same_as<Result, ExecutionCompletion>) {
        return std::move(result);
    } else {
        static_assert(std::same_as<Result, ExecutionValue>);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        if (result->flow != ExecutionFlow::Normal) {
            return std::unexpected(
                fail(origin, ExecutionReason::Evaluation, "control transfer escaped an operand")
            );
        }
        return std::move(result->value);
    }
}

template<typename Result, typename Operation>
auto SemanticExecutor::expression_operation(
    ExecutionFrame& frame,
    const SemanticExpression& source,
    const Operation& operation
) noexcept -> ExecutionTask<Result> {
    if constexpr (std::same_as<Operation, SemUnreachable>) {
        invariant_violation("execution reached a semantic edge without an entry");
    } else {
        if (auto checked = step(source.origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (const auto reason = unsupported_execution_operation(operation)) {
            co_return control_result<Result>(
                (co_await unsupported_expression(frame, source, *reason)),
                source.origin
            );
        }
        if constexpr (std::same_as<Operation, SemConstant>) {
            co_return normal_result<Result>(ExecutionValue(operation.constant));
        } else if constexpr (std::same_as<Operation, SemBinding>) {
            auto local = slot_value(frame, operation.binding.index(), source.origin);
            if (!local) {
                co_return std::unexpected(std::move(local.error()));
            }
            co_return normal_result<Result>(copy_value(**local, source.origin));
        } else if constexpr (std::same_as<Operation, SemUnary>
                             || std::same_as<Operation, SemCast>) {
            auto operand = (co_await value(frame, *operation.operand));
            if (!operand) {
                co_return std::unexpected(std::move(operand.error()));
            }
            auto result = [&]() noexcept {
                if constexpr (std::same_as<Operation, SemUnary>) {
                    return finish_unary_value(source, operation.operation, *operand);
                } else {
                    return finish_cast_value(source, operation.kind, std::move(*operand));
                }
            }();
            co_return normal_result<Result>(std::move(result));
        } else if constexpr (std::same_as<Operation, SemBinary>) {
            auto left = (co_await value(frame, *operation.left));
            if (!left) {
                co_return std::unexpected(std::move(left.error()));
            }
            auto right = (co_await value(frame, *operation.right));
            if (!right) {
                co_return std::unexpected(std::move(right.error()));
            }
            co_return normal_result<Result>(finish_binary_value(source, operation, *left, *right));
        } else if constexpr (std::same_as<Operation, SemPropagate>) {
            co_return control_result<Result>(
                (co_await expression(frame, *operation.operand)),
                source.origin
            );
        } else if constexpr (std::same_as<Operation, SemTry>) {
            co_return control_result<Result>(
                (co_await try_expression(frame, operation)),
                source.origin
            );
        } else if constexpr (std::same_as<Operation, SemIf>) {
            co_return control_result<Result>(
                (co_await if_expression(frame, operation)),
                source.origin
            );
        } else if constexpr (std::same_as<Operation, SemMatch>) {
            co_return control_result<Result>(
                (co_await match_expression(frame, operation)),
                source.origin
            );
        } else {
            co_return normal_result<Result>((co_await expression_value(frame, source, operation)));
        }
    }
}

template<typename Result>
auto SemanticExecutor::evaluate_expression(
    ExecutionFrame& frame,
    const SemanticExpression& source
) noexcept -> ExecutionTask<Result> {
    static_assert(
        std::same_as<Result, ExecutionValue> || std::same_as<Result, ExecutionCompletion>
    );
    return source.value.visit([&](const auto& operation) noexcept {
        return expression_operation<Result>(frame, source, operation);
    });
}

auto SemanticExecutor::value(ExecutionFrame& frame, const SemanticExpression& source) noexcept
    -> ExecutionTask<ExecutionValue> {
    return evaluate_expression<ExecutionValue>(frame, source);
}

auto SemanticExecutor::expression(ExecutionFrame& frame, const SemanticExpression& source) noexcept
    -> ExecutionTask<ExecutionCompletion> {
    return evaluate_expression<ExecutionCompletion>(frame, source);
}

auto SemanticExecutor::matches(
    ExecutionFrame& frame,
    PatternID id,
    const ExecutionValue& subject,
    std::span<const SemPatternBounds> pattern_bounds
) noexcept -> ExecutionTask<bool> {
    co_return (co_await frame.body->visit_pattern(
        id,
        [&](const auto& pattern) noexcept -> ExecutionTask<bool> {
            if (auto checked = step(pattern.origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            co_return (co_await pattern.value.visit(
                [&](const auto& pattern_value) noexcept -> ExecutionTask<bool> {
                    using Pattern = std::remove_cvref_t<decltype(pattern_value)>;
                    if constexpr (std::same_as<Pattern, WildcardPattern>) {
                        co_return true;
                    } else if constexpr (std::same_as<Pattern, BindingPattern>) {
                        auto copied = copy_value(subject, pattern.origin);
                        if (!copied) {
                            co_return std::unexpected(std::move(copied.error()));
                        }
                        bind(frame, pattern_value.binding.index(), std::move(*copied));
                        co_return true;
                    } else if constexpr (std::same_as<Pattern, TypeConstraintPattern>
                                         || std::
                                             same_as<Pattern, ElaboratedTypeConstraintPattern>) {
                        auto expected =
                            type(ConstructionTypeRef(pattern_value.type), pattern.origin);
                        if (!expected) {
                            co_return std::unexpected(std::move(expected.error()));
                        }
                        co_return execution_value_type(values, subject)
                            == ConstructionTypeRef(*expected);
                    } else if constexpr (std::same_as<Pattern, EnumCasePattern>) {
                        const auto compound = execution_compound_view(values, subject);
                        if (!compound) {
                            const auto atom = execution_atom(values, subject);
                            const auto* numeric =
                                atom ? std::get_if<NumericEnumConstant>(&atom->value) : nullptr;
                            co_return numeric != nullptr
                                && numeric->enum_case == pattern_value.enum_case;
                        }
                        if (compound->enum_case != pattern_value.enum_case
                            || compound->size() != pattern_value.payload.size()) {
                            co_return false;
                        }
                        co_return (co_await compound->elements.visit(
                            [&](const auto children) noexcept -> ExecutionTask<bool> {
                                for (auto index = 0uz; index < children.size(); ++index) {
                                    auto accepted = (co_await matches(
                                        frame,
                                        pattern_value.payload[index],
                                        children[index],
                                        pattern_bounds
                                    ));
                                    if (!accepted || !*accepted) {
                                        co_return accepted;
                                    }
                                }
                                co_return true;
                            }
                        ));
                    } else if constexpr (std::same_as<Pattern, RangePattern>) {
                        const auto read = [&](const RangePatternBound& bound,
                                              bool upper) noexcept -> ExecutionTask<ConstantFact> {
                            if (bound.constant) {
                                co_return values.constant(*bound.constant);
                            }
                            const auto found =
                                std::ranges::find(pattern_bounds, id, &SemPatternBounds::pattern);
                            if (found == pattern_bounds.end()) {
                                co_return std::unexpected(fail(
                                    pattern.origin,
                                    ExecutionReason::Evaluation,
                                    "missing range bound"
                                ));
                            }
                            const auto& expression = upper ? found->end : found->begin;
                            auto result = (co_await this->value(frame, *expression));
                            if (!result) {
                                co_return std::unexpected(std::move(result.error()));
                            }
                            co_return read_fact(*result, pattern.origin);
                        };
                        auto first = std::optional<ConstantFact>();
                        auto last = std::optional<ConstantFact>();
                        if (pattern_value.begin) {
                            auto result = (co_await read(*pattern_value.begin, false));
                            if (!result) {
                                co_return std::unexpected(std::move(result.error()));
                            }
                            first = std::move(*result);
                        }
                        if (pattern_value.end) {
                            auto result = (co_await read(*pattern_value.end, true));
                            if (!result) {
                                co_return std::unexpected(std::move(result.error()));
                            }
                            last = std::move(*result);
                        }
                        auto selected = read_fact(subject, pattern.origin);
                        if (!selected) {
                            co_return std::unexpected(std::move(selected.error()));
                        }
                        const auto compare =
                            [&](BinaryOperator operation,
                                const ConstantFact& bound) noexcept -> ExecutionResult<bool> {
                            auto result = finish(
                                evaluate_binary_constant_value(
                                    values,
                                    operation,
                                    *selected,
                                    bound,
                                    values.builtin_type(BuiltinType::Bool)
                                ),
                                pattern.origin
                            );
                            if (!result) {
                                return std::unexpected(std::move(result.error()));
                            }
                            return boolean(*result, pattern.origin);
                        };
                        if (first) {
                            auto accepted = (compare(BinaryOperator::GreaterEqual, *first));
                            if (!accepted || !*accepted) {
                                co_return accepted;
                            }
                        }
                        co_return last ? (compare(
                                             pattern_value.inclusive ? BinaryOperator::LessEqual
                                                                     : BinaryOperator::Less,
                                             *last
                                         ))
                                       : ExecutionResult<bool>(true);
                    } else if constexpr (std::same_as<Pattern, LiteralPattern>) {
                        co_return equal(
                            subject,
                            ExecutionValue(pattern_value.constant),
                            pattern.origin
                        );
                    } else if constexpr (std::same_as<Pattern, OrPattern>) {
                        for (const auto alternative : pattern_value.alternatives) {
                            auto accepted =
                                (co_await matches(frame, alternative, subject, pattern_bounds));
                            if (!accepted || *accepted) {
                                co_return accepted;
                            }
                        }
                        co_return false;
                    } else {
                        co_return std::unexpected(fail(
                            pattern.origin,
                            ExecutionReason::Evaluation,
                            "pattern is not supported in execution"
                        ));
                    }
                }
            ));
        }
    ));
}
