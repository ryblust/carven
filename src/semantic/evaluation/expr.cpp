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
    return finish(
        evaluate_unary_constant_value(
            values,
            operation,
            *fact,
            *target,
            IntegerArithmetic::Wrapping
        ),
        source.origin
    );
}

auto SemanticExecutor::finish_cast_value(
    const SemanticExpression& source,
    CastKind kind,
    ExecutionValue&& operand
) noexcept -> ExecutionResult<ExecutionValue> {
    if (kind == CastKind::Identity) {
        return std::move(operand);
    }
    if (kind == CastKind::PointerRead
        || kind == CastKind::PointerErase
        || kind == CastKind::PointerRestore) {
        const auto target = source.type.construction();
        if (std::holds_alternative<ExecutionPointer>(operand)) {
            return std::move(operand);
        }
        const auto atom = execution_atom(values, operand);
        if (kind == CastKind::PointerErase && atom) {
            if (const auto* text = std::get_if<CStringConstant>(&atom->value)) {
                auto found = retained_cstring_backings.find(text->value);
                if (found == retained_cstring_backings.end()) {
                    // This coordinate denotes native character backing, not a
                    // const char* object that restoration may dereference.
                    found = retained_cstring_backings
                                .emplace(text->value, memory.create(ExecutionVoid {}))
                                .first;
                }
                return ExecutionPointer {.type = target, .target = found->second};
            }
        }
        if (!atom || !std::holds_alternative<NullPointerConstant>(atom->value)) {
            return std::unexpected(
                fail(source.origin, ExecutionReason::Evaluation, "expected a pointer operand")
            );
        }
        return ExecutionPointer {.type = target, .target = std::nullopt};
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
        evaluate_binary_constant_value(
            values,
            operation.operation,
            *lhs,
            *rhs,
            *target,
            IntegerArithmetic::Wrapping
        ),
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
    auto protected_result = (co_await region(frame, *attempt.body, true));
    if (protected_result) {
        co_return protected_result;
    }
    const auto* source_failure = std::get_if<ExecutionSourceFailure>(&protected_result.error());
    if (source_failure == nullptr) {
        co_return protected_result;
    }
    // Keep the original failure separate from writable pattern projections and rethrow.
    const auto failure = *source_failure;
    auto payload = copy_value(*failure.payload, attempt.body->origin, CopyMode::Value);
    if (!payload) {
        co_return std::unexpected(std::move(payload.error()));
    }
    const auto payload_place = memory.create(std::move(*payload));

    struct ReleasePayload final {
        ExecutionMemory& memory;
        ExecutionPlace place;

        ~ReleasePayload() { memory.release(place); }
    };

    const auto payload_scope = ReleasePayload {.memory = memory, .place = payload_place};
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
                auto accepted = (co_await matches(
                    frame,
                    typed->inner,
                    *memory.resolve(payload_place),
                    arm.pattern_bounds,
                    payload_place
                ));
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
            auto result = (co_await region(frame, arm.body, true));
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
            co_return (co_await region(frame, branch.body, true));
        }
    }
    co_return conditional.otherwise
        ? (co_await region(frame, **conditional.otherwise, true))
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
    auto owned_subject = std::optional<ExecutionPlace>();

    struct ReleaseSubject final {
        ExecutionMemory& memory;
        std::optional<ExecutionPlace>& place;

        ~ReleaseSubject() {
            if (place) {
                memory.release(*place);
            }
        }
    };

    const auto subject_scope = ReleaseSubject {.memory = memory, .place = owned_subject};
    if (!selected) {
        auto subject = co_await this->value(frame, *match.subject);
        if (!subject) {
            co_return std::unexpected(std::move(subject.error()));
        }
        if (std::holds_alternative<ConstantID>(*subject)
            && execution_compound_view(values, *subject)) {
            subject = copy_value(*subject, match.subject->origin, CopyMode::Value);
            if (!subject) {
                co_return std::unexpected(std::move(subject.error()));
            }
        }
        owned_subject = memory.create(std::move(*subject));
        selected = owned_subject;
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
        auto target = located(*selected, match.subject->origin);
        if (!target) {
            co_return std::unexpected(std::move(target.error()));
        }
        const auto pattern_temporaries = frame.temporaries.size();
        auto accepted =
            (co_await matches(frame, arm.pattern, **target, arm.pattern_bounds, selected));
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
        auto result = (co_await region(frame, arm.body, true));
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
        } else if constexpr (std::same_as<Operation, SemClosure>) {
            for (const auto& capture : operation.captures) {
                inputs.push_back({
                    .expression = &capture.expression,
                    .use =
                        capture.mode == CaptureMode::Write ? OperandUse::Write : OperandUse::Value,
                });
            }
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
        co_return (co_await default_value(source.type.construction(), source.origin));
    } else if constexpr (std::same_as<Operation, SemCallable>) {
        co_return ExecutionFunction {
            .type = source.type.construction(),
            .callable = operation.callable
        };
    } else if constexpr (std::same_as<Operation, SemBorrowCallable>) {
        if (auto supported = check_callable_adaptation(
                operation.source->type.construction(),
                source.type.construction(),
                source.origin
            );
            !supported) {
            co_return std::unexpected(std::move(supported.error()));
        }
        auto function = (co_await this->value(frame, *operation.source));
        if (!function) {
            co_return std::unexpected(std::move(function.error()));
        }
        if (std::holds_alternative<ExecutionFunction>(*function)) {
            co_return std::move(*function);
        }
        co_return std::unexpected(fail(
            source.origin,
            ExecutionReason::Evaluation,
            "callable is not supported in execution"
        ));
    } else if constexpr (std::same_as<Operation, SemArrayAdopt>) {
        if (auto supported = check_callable_adaptation(
                operation.source->type.construction(),
                source.type.construction(),
                source.origin
            );
            !supported) {
            co_return std::unexpected(std::move(supported.error()));
        }
        co_return (co_await this->value(frame, *operation.source));
    } else if constexpr (std::same_as<Operation, SemAddressOf>) {
        auto selected = (co_await place(frame, *operation.source));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        if (auto target = located(*selected, source.origin); !target) {
            co_return std::unexpected(std::move(target.error()));
        }
        co_return ExecutionPointer {
            .type = source.type.construction(),
            .target = std::move(*selected)
        };
    } else if constexpr (std::same_as<Operation, SemDereference>) {
        auto selected = (co_await place(frame, source));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        auto target = located(*selected, source.origin);
        if (!target) {
            co_return std::unexpected(std::move(target.error()));
        }
        co_return copy_value(**target, source.origin, CopyMode::Value);
    } else if constexpr (std::same_as<Operation, SemTake>) {
        auto selected = (co_await place(frame, *operation.place));
        if (!selected) {
            co_return std::unexpected(std::move(selected.error()));
        }
        auto local = located(*selected, source.origin);
        if (!local) {
            co_return std::unexpected(std::move(local.error()));
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
        const auto result_type = source.type.construction();
        if (auto checked = check_aggregate_size(result_type, source.origin); !checked) {
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
        co_return bounded_value(
            ExecutionAggregateValue {
                .type = result_type,
                .elements = std::move(elements),
                .capacity = 0uz
            },
            source.origin
        );
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
            co_return copy_value(**target, source.origin, CopyMode::Value);
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
        co_return bounded_value(
            ExecutionEnumValue {
                .type = *target,
                .enum_case = operation.enum_case,
                .payload = std::move(elements)
            },
            source.origin
        );
    } else if constexpr (std::same_as<Operation, SemIntrinsic>) {
        if (const auto* intrinsic = std::get_if<FloatIntrinsic>(&operation.operation)) {
            auto facts = std::vector<ConstantFact>();
            for (const auto& operand : operation.operands) {
                auto input = co_await this->value(frame, operand.expression);
                if (!input) {
                    co_return std::unexpected(std::move(input.error()));
                }
                auto fact = read_fact(*input, source.origin);
                if (!fact) {
                    co_return std::unexpected(std::move(fact.error()));
                }
                facts.push_back(std::move(*fact));
            }
            auto target = type(source.type.construction(), source.origin);
            if (!target) {
                co_return std::unexpected(std::move(target.error()));
            }
            co_return finish(
                evaluate_float_intrinsic_constant_value(values, *intrinsic, facts, *target),
                source.origin
            );
        }
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
        if (std::holds_alternative<VectorIntrinsicOperation>(operation.operation)) {
            auto target = type(source.type.construction(), source.origin);
            if (!target) {
                co_return std::unexpected(std::move(target.error()));
            }
            co_return co_await vector_intrinsic(frame, operation, *target, source.origin);
        }
        const auto intrinsic = std::get<SliceIntrinsicOperation>(operation.operation).intrinsic;
        auto sequence =
            (co_await sequence_view(frame, operation.operands[0].expression, source.origin));
        if (!sequence) {
            co_return std::unexpected(std::move(sequence.error()));
        }
        const auto target = source.type.construction();
        switch (intrinsic) {
            case SliceIntrinsic::Length: {
                auto concrete = type(target, source.origin);
                if (!concrete) {
                    co_return std::unexpected(std::move(concrete.error()));
                }
                co_return ConstantAtom {
                    .type = *concrete,
                    .value = IntegerConstant::from_parts(sequence->extent, false)
                };
            }
            case SliceIntrinsic::IsEmpty: {
                auto concrete = type(target, source.origin);
                if (!concrete) {
                    co_return std::unexpected(std::move(concrete.error()));
                }
                co_return ConstantAtom {
                    .type = *concrete,
                    .value = BooleanConstant {.value = sequence->extent == 0uz}
                };
            }
            case SliceIntrinsic::View:
            case SliceIntrinsic::Slice: {
                if (intrinsic == SliceIntrinsic::View && sequence->extent == 0uz) {
                    co_return ExecutionAggregateValue {
                        .type = target,
                        .elements = {},
                        .capacity = 0uz
                    };
                }
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
                    .type = target,
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
    } else if constexpr (std::same_as<Operation, SemArray> || std::same_as<Operation, SemTuple>) {
        if constexpr (std::same_as<Operation, SemTuple>) {
            if (operation.elements.empty()) {
                co_return ExecutionVoid {};
            }
        }
        const auto result_type = source.type.construction();
        if (auto checked = check_aggregate_size(result_type, source.origin); !checked) {
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
        co_return bounded_value(
            ExecutionAggregateValue {
                .type = result_type,
                .elements = std::move(elements),
                .capacity = operation.elements.size()
            },
            source.origin
        );
    } else if constexpr (std::same_as<Operation, SemTupleElement>) {
        if (has_bound_storage(source)) {
            auto selected = co_await place(frame, source);
            if (!selected) {
                co_return std::unexpected(std::move(selected.error()));
            }
            auto target = located(*selected, source.origin);
            if (!target) {
                co_return std::unexpected(std::move(target.error()));
            }
            co_return copy_value(**target, source.origin, CopyMode::Value);
        }
        auto receiver = co_await this->value(frame, *operation.source);
        if (!receiver) {
            co_return std::unexpected(std::move(receiver.error()));
        }
        const auto product = execution_compound_view(values, *receiver);
        if (!product || operation.index >= product->size()) {
            co_return std::unexpected(fail(
                source.origin,
                ExecutionReason::Evaluation,
                "tuple projection requires a product slot"
            ));
        }
        if (const auto* constants = std::get_if<std::span<const ConstantID>>(&product->elements)) {
            co_return ExecutionValue((*constants)[operation.index]);
        }
        co_return std::move(execution_elements(*receiver)[operation.index]);
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
            co_return copy_value(**target, source.origin, CopyMode::Value);
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
            co_return copy_value(**element, source.origin, CopyMode::Value);
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
    } else if constexpr (std::same_as<Operation, SemColdCompose>) {
        auto arguments = std::vector<ExecutionOperand>();
        arguments.reserve(operation.inputs.size());
        for (const auto& input : operation.inputs) {
            auto evaluated = co_await operand(
                frame,
                input.expression,
                argument_use(input.access),
                source.origin
            );
            if (!evaluated) {
                co_return std::unexpected(std::move(evaluated.error()));
            }
            arguments.push_back(std::move(*evaluated));
        }
        co_return std::make_unique<ExecutionColdOperation>(ExecutionColdOperation {
            .type = source.type.construction(),
            .action =
                ExecutionCompositionAction {
                    .policy = operation.policy,
                    .success = operation.success.construction(),
                    .selection = ExecutionSelectionResult::Indexed,
                },
            .arguments = std::move(arguments),
        });
    } else if constexpr (std::same_as<Operation, SemColdTimeout>) {
        auto input =
            co_await operand(frame, *operation.operation, OperandUse::Value, source.origin);
        if (!input) {
            co_return std::unexpected(std::move(input.error()));
        }
        auto duration =
            co_await operand(frame, *operation.duration_ticks, OperandUse::Value, source.origin);
        if (!duration) {
            co_return std::unexpected(std::move(duration.error()));
        }
        auto arguments = std::vector<ExecutionOperand>();
        arguments.push_back(std::move(*input));
        arguments.push_back(std::move(*duration));
        co_return std::make_unique<ExecutionColdOperation>(ExecutionColdOperation {
            .type = source.type.construction(),
            .action =
                ExecutionTimeoutAction {
                    .timeout_failure = operation.timeout_failure,
                    .range_failure = operation.range_failure
                },
            .arguments = std::move(arguments),
        });
    } else if constexpr (std::same_as<Operation, SemAsyncIntrinsic>) {
        switch (operation.kind) {
            case AsyncIntrinsic::CancelRequested: {
                auto result_type = type(source.type.construction(), source.origin);
                if (!result_type) {
                    co_return std::unexpected(std::move(result_type.error()));
                }
                co_return ConstantAtom {
                    .type = *result_type,
                    .value = BooleanConstant {.value = cancel_requested()},
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
                request_cancel(*child->task);
                co_return ExecutionVoid {};
            }
            case AsyncIntrinsic::YieldOnce:
            case AsyncIntrinsic::CancelPoint:
                co_return std::make_unique<ExecutionColdOperation>(ExecutionColdOperation {
                    .type = source.type.construction(),
                    .action = operation.kind,
                    .arguments = {},
                });
            case AsyncIntrinsic::WaitReady:
            case AsyncIntrinsic::CloseChild:
                invariant_violation("direct child coordination stored as a cold operation");
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
    } else if constexpr (std::same_as<Operation, SemWaitReady>) {
        co_return co_await wait_ready(frame, operation, source);
    } else if constexpr (std::same_as<Operation, SemCloseChild>) {
        co_return co_await close_child(frame, operation.child, source.origin);
    } else if constexpr (std::same_as<Operation, SemPrint>) {
        co_return (co_await print(frame, operation, source.origin));
    } else if constexpr (std::same_as<Operation, SemReport>) {
        co_return (co_await report(frame, operation, source.origin));
    } else if constexpr (std::same_as<Operation, SemFormat>) {
        co_return (co_await format(frame, operation, source.origin));
    } else {
        static_assert(std::same_as<Operation, void>, "unhandled execution operation");
    }
}

template<typename Result>
auto SemanticExecutor::normal_result(
    ExecutionResult<ExecutionValue>&& result,
    const SemanticExpression& source
) noexcept
    -> std::conditional_t<
        std::same_as<Result, ExecutionValue>,
        ExecutionResult<ExecutionValue>&&,
        ExecutionResult<Result>> {
    if (result) {
        if (auto typed = deliver_value(*result, source.type.construction(), source.origin);
            !typed) {
            result = std::unexpected(std::move(typed.error()));
        }
    }
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
    const SemanticExpression& source
) noexcept
    -> std::conditional_t<
        std::same_as<Result, ExecutionCompletion>,
        ExecutionResult<ExecutionCompletion>&&,
        ExecutionResult<Result>> {
    if (result && result->flow == ExecutionFlow::Normal) {
        if (auto typed = deliver_value(result->value, source.type.construction(), source.origin);
            !typed) {
            result = std::unexpected(std::move(typed.error()));
        }
    }
    if constexpr (std::same_as<Result, ExecutionCompletion>) {
        return std::move(result);
    } else {
        static_assert(std::same_as<Result, ExecutionValue>);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
        if (result->flow != ExecutionFlow::Normal) {
            return std::unexpected(fail(
                source.origin,
                ExecutionReason::Evaluation,
                "control transfer escaped an operand"
            ));
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
    } else if constexpr (std::same_as<Operation, SemGenericCall>) {
        invariant_violation("unsubstituted generic call reached execution");
    } else {
        if (auto checked = step(source.origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        constexpr auto reason = unsupported_execution_operation<Operation>();
        if constexpr (reason.has_value()) {
            co_return control_result<Result>(
                (co_await unsupported_expression(frame, source, *reason)),
                source
            );
        } else if constexpr (std::same_as<Operation, SemConstant>) {
            co_return normal_result<Result>(ExecutionValue(operation.constant), source);
        } else if constexpr (std::same_as<Operation, SemBinding>) {
            auto local = slot_value(frame, operation.binding.index(), source.origin);
            if (!local) {
                co_return std::unexpected(std::move(local.error()));
            }
            co_return normal_result<Result>(
                copy_value(**local, source.origin, CopyMode::Value),
                source
            );
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
            co_return normal_result<Result>(std::move(result), source);
        } else if constexpr (std::same_as<Operation, SemBinary>) {
            auto left = (co_await value(frame, *operation.left));
            if (!left) {
                co_return std::unexpected(std::move(left.error()));
            }
            auto right = (co_await value(frame, *operation.right));
            if (!right) {
                co_return std::unexpected(std::move(right.error()));
            }
            co_return normal_result<Result>(
                finish_binary_value(source, operation, *left, *right),
                source
            );
        } else if constexpr (std::same_as<Operation, SemPropagate>) {
            co_return control_result<Result>(
                (co_await expression(frame, *operation.operand)),
                source
            );
        } else if constexpr (std::same_as<Operation, SemTry>) {
            co_return control_result<Result>((co_await try_expression(frame, operation)), source);
        } else if constexpr (std::same_as<Operation, SemIf>) {
            co_return control_result<Result>((co_await if_expression(frame, operation)), source);
        } else if constexpr (std::same_as<Operation, SemMatch>) {
            co_return control_result<Result>((co_await match_expression(frame, operation)), source);
        } else {
            co_return normal_result<Result>(
                (co_await expression_value(frame, source, operation)),
                source
            );
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
    std::span<const SemPatternBounds> pattern_bounds,
    std::optional<ExecutionPlace> subject_place
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
                        const auto storage = frame.body->binding_storage(pattern_value.binding);
                        if (const auto* access = std::get_if<AccessBindingStorage>(&storage);
                            access != nullptr
                            && (access->access == AccessMode::Write
                                || read_borrows_storage(
                                    frame.body->binding_type(pattern_value.binding)
                                ))) {
                            if (!subject_place) {
                                co_return std::unexpected(fail(
                                    pattern.origin,
                                    ExecutionReason::Evaluation,
                                    "pattern access requires live subject storage"
                                ));
                            }
                            release(frame, pattern_value.binding.index());
                            frame.slots[pattern_value.binding.index()] = *subject_place;
                            co_return true;
                        }
                        auto copied = copy_value(subject, pattern.origin, CopyMode::Value);
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
                    } else if constexpr (std::same_as<Pattern, TuplePattern>) {
                        if (pattern_value.elements.empty()) {
                            co_return execution_value_type(values, subject)
                                == ConstructionTypeRef(values.builtin_type(BuiltinType::Void));
                        }
                        const auto compound = execution_compound_view(values, subject);
                        if (!compound
                            || compound->enum_case
                            || compound->size() != pattern_value.elements.size()) {
                            co_return false;
                        }
                        co_return (co_await compound->elements.visit(
                            [&](const auto children) noexcept -> ExecutionTask<bool> {
                                for (auto index = 0uz; index < children.size(); ++index) {
                                    auto accepted = (co_await matches(
                                        frame,
                                        pattern_value.elements[index],
                                        children[index],
                                        pattern_bounds,
                                        subject_place ? memory.project(*subject_place, index)
                                                      : std::nullopt
                                    ));
                                    if (!accepted || !*accepted) {
                                        co_return accepted;
                                    }
                                }
                                co_return true;
                            }
                        ));
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
                                        pattern_bounds,
                                        subject_place ? memory.project(*subject_place, index)
                                                      : std::nullopt
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
                                    values.builtin_type(BuiltinType::Bool),
                                    IntegerArithmetic::Wrapping
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
                            auto accepted = (co_await matches(
                                frame,
                                alternative,
                                subject,
                                pattern_bounds,
                                subject_place
                            ));
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
