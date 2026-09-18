module carven:semantic.evaluation.expr.impl;

import :semantic.evaluation.executor;
import std;

auto SemanticExecutor::value(ExecutionFrame& frame, const SemanticExpression& source) noexcept
    -> ExecutionTask<ExecutionValue> {
    auto result = (co_await expression(frame, source));
    if (!result) {
        co_return std::unexpected(result.error());
    }
    if (result->flow != ExecutionFlow::Normal) {
        co_return std::unexpected(fail(
            source.origin,
            DiagnosticCode::ConstEvaluation,
            "control transfer escaped a operand"
        ));
    }
    co_return std::move(result->value);
}

auto SemanticExecutor::expression(ExecutionFrame& frame, const SemanticExpression& source) noexcept
    -> ExecutionTask<ExecutionCompletion> {
    if (auto checked = step(source.origin); !checked) {
        co_return std::unexpected(checked.error());
    }
    if (const auto* propagation = std::get_if<SemPropagate>(&source.value)) {
        co_return (co_await expression(frame, *propagation->operand));
    }
    if (const auto* attempt = std::get_if<SemTry>(&source.value)) {
        auto protected_result = (co_await region(frame, *attempt->body));
        if (protected_result) {
            co_return protected_result;
        }
        const auto* source_failure = std::get_if<ExecutionSourceFailure>(&protected_result.error());
        if (source_failure == nullptr) {
            co_return protected_result;
        }
        // The original payload remains independent of copied bindings and nested catches.
        const auto failure = *source_failure;
        for (const auto& arm : attempt->arms) {
            const auto reset = [&]() noexcept {
                for (const auto binding : arm.bindings) {
                    frame.slots[binding.index()] = ExecutionUninitialized {};
                }
            };
            auto matched = false;
            for (const auto& alternative : arm.alternatives) {
                if (!alternative.reachable) {
                    continue;
                }
                reset();
                if (std::holds_alternative<CatchAllPattern>(alternative.pattern)) {
                    matched = true;
                } else if (const auto* typed =
                               std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                    const auto catch_type = type(typed->type.construction(), alternative.origin);
                    if (!catch_type) {
                        co_return std::unexpected(catch_type.error());
                    }
                    if (*catch_type != failure.type) {
                        continue;
                    }
                    auto accepted = (co_await matches(
                        frame,
                        typed->inner,
                        *failure.payload,
                        arm.pattern_bounds
                    ));
                    if (!accepted) {
                        reset();
                        co_return std::unexpected(accepted.error());
                    }
                    matched = *accepted;
                }
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
                    auto guard = (co_await value(frame, *arm.guard));
                    if (!guard) {
                        co_return std::unexpected(guard.error());
                    }
                    auto truth = boolean(*guard, arm.guard->origin);
                    if (!truth) {
                        co_return std::unexpected(truth.error());
                    }
                    if (!*truth) {
                        co_return std::nullopt;
                    }
                }
                auto result = (co_await region(frame, arm.body));
                if (!result) {
                    co_return std::unexpected(result.error());
                }
                co_return std::move(*result);
            }();
            frame.caught.pop_back();
            reset();
            if (!recover) {
                co_return std::unexpected(recover.error());
            }
            if (*recover) {
                co_return std::move(**recover);
            }
        }
        co_return protected_result;
    }
    if (const auto* conditional = std::get_if<SemIf>(&source.value)) {
        for (const auto& branch : conditional->branches) {
            auto condition = (co_await value(frame, branch.condition));
            if (!condition) {
                co_return std::unexpected(condition.error());
            }
            auto truth = boolean(*condition, branch.condition.origin);
            if (!truth) {
                co_return std::unexpected(truth.error());
            }
            if (*truth) {
                co_return (co_await region(frame, branch.body));
            }
        }
        co_return conditional->otherwise
            ? (co_await region(frame, **conditional->otherwise))
            : ExecutionResult<ExecutionCompletion>(
                  ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}}
              );
    }
    if (const auto* match = std::get_if<SemMatch>(&source.value)) {
        auto selected = std::optional<ExecutionPlace>();
        if (match->subject_is_place) {
            auto target = (co_await place(frame, *match->subject));
            if (!target) {
                co_return std::unexpected(target.error());
            }
            selected = std::move(*target);
        }
        auto subject = selected ? ExecutionResult<ExecutionValue>(ExecutionVoid {})
                                : (co_await value(frame, *match->subject));
        if (!subject) {
            co_return std::unexpected(subject.error());
        }
        for (const auto& arm : match->arms) {
            if (!arm.reachable) {
                continue;
            }
            if (selected) {
                auto target = located(frame, *selected, match->subject->origin);
                if (!target) {
                    co_return std::unexpected(target.error());
                }
                subject = copy_value(**target, match->subject->origin);
                if (!subject) {
                    co_return std::unexpected(subject.error());
                }
            }
            auto accepted = (co_await matches(frame, arm.pattern, *subject, arm.pattern_bounds));
            if (!accepted) {
                co_return std::unexpected(accepted.error());
            }
            if (!*accepted) {
                continue;
            }
            if (arm.guard) {
                auto guard = (co_await value(frame, *arm.guard));
                if (!guard) {
                    co_return std::unexpected(guard.error());
                }
                auto truth = boolean(*guard, arm.guard->origin);
                if (!truth) {
                    co_return std::unexpected(truth.error());
                }
                if (!*truth) {
                    continue;
                }
            }
            co_return (co_await region(frame, arm.body));
        }
        co_return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
    }
    auto result = (co_await source.value.visit(
        [&](const auto& operation) noexcept -> ExecutionTask<ExecutionValue> {
            using Operation = std::remove_cvref_t<decltype(operation)>;
            if constexpr (std::same_as<Operation, SemDefault>) {
                const auto target = type(source.type.construction(), source.origin);
                if (!target) {
                    co_return std::unexpected(target.error());
                }
                co_return (co_await default_value(*target, source.origin));
            } else if constexpr (std::same_as<Operation, SemConstant>) {
                co_return ExecutionValue(operation.constant);
            } else if constexpr (std::same_as<Operation, SemBinding>) {
                auto local = slot_value(frame, operation.binding.index(), source.origin);
                if (!local) {
                    co_return std::unexpected(local.error());
                }
                co_return copy_value(**local, source.origin);
            } else if constexpr (std::same_as<Operation, SemTake>) {
                auto selected = (co_await place(frame, *operation.place));
                if (!selected) {
                    co_return std::unexpected(selected.error());
                }
                auto local = located(frame, *selected, source.origin);
                if (!local) {
                    co_return std::unexpected(local.error());
                }
                if (!selected->path.empty()) {
                    co_return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "take requires a whole binding"
                    ));
                }
                auto result = std::move(**local);
                frame.slots[selected->slot] = ExecutionTaken {};
                co_return result;
            } else if constexpr (std::same_as<Operation, SemStruct>) {
                auto result_type = type(source.type.construction(), source.origin);
                if (!result_type) {
                    co_return std::unexpected(result_type.error());
                }
                if (auto checked = check_aggregate_size(*result_type, source.origin); !checked) {
                    co_return std::unexpected(checked.error());
                }
                if (auto checked = account_aggregate(operation.fields.size(), source.origin);
                    !checked) {
                    co_return std::unexpected(checked.error());
                }
                auto elements =
                    std::vector<ExecutionValue>(operation.fields.size(), ExecutionVoid {});
                for (const auto& field : operation.fields) {
                    auto evaluated = (co_await value(frame, field.value));
                    if (!evaluated) {
                        co_return std::unexpected(evaluated.error());
                    }
                    elements.at(field.declaration_index) = std::move(*evaluated);
                }
                co_return ExecutionAggregateValue {
                    .type = *result_type,
                    .elements = std::move(elements)
                };
            } else if constexpr (std::same_as<Operation, SemField>) {
                if (local_place(source)) {
                    auto selected = (co_await place(frame, source));
                    if (!selected) {
                        co_return std::unexpected(selected.error());
                    }
                    auto target = located(frame, *selected, source.origin);
                    if (!target) {
                        co_return std::unexpected(target.error());
                    }
                    co_return copy_value(**target, source.origin);
                }
                auto receiver = (co_await value(frame, *operation.source));
                if (!receiver) {
                    co_return std::unexpected(receiver.error());
                }
                if (const auto* retained = std::get_if<ConstantID>(&*receiver)) {
                    const auto& fact = values.constant(*retained);
                    if (const auto* fields = std::get_if<StructConstant>(&fact.value)) {
                        co_return ExecutionValue(fields->fields.at(operation.field.field_index));
                    }
                }
                auto* aggregate = std::get_if<ExecutionAggregateValue>(&*receiver);
                if (aggregate == nullptr
                    || operation.field.field_index >= aggregate->elements.size()) {
                    co_return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "field requires a structure value"
                    ));
                }
                co_return std::move(aggregate->elements[operation.field.field_index]);
            } else if constexpr (std::same_as<Operation, SemEnumCase>) {
                auto target = type(source.type.construction(), source.origin);
                if (!target) {
                    co_return std::unexpected(target.error());
                }
                if (auto checked = check_aggregate_size(*target, source.origin); !checked) {
                    co_return std::unexpected(checked.error());
                }
                if (auto checked = account_aggregate(operation.payload.size(), source.origin);
                    !checked) {
                    co_return std::unexpected(checked.error());
                }
                auto elements = std::vector<ExecutionValue>();
                for (const auto& child : operation.payload) {
                    auto evaluated = (co_await value(frame, child));
                    if (!evaluated) {
                        co_return std::unexpected(evaluated.error());
                    }
                    elements.push_back(std::move(*evaluated));
                }
                co_return ExecutionEnumValue {
                    .type = *target,
                    .enum_case = operation.enum_case,
                    .payload = std::move(elements)
                };
            } else if constexpr (std::same_as<Operation, SemSliceIntrinsic>) {
                auto receiver = (co_await value(frame, operation.operands[0].expression));
                if (!receiver) {
                    co_return std::unexpected(receiver.error());
                }
                const auto sequence = execution_compound_view(values, *receiver);
                if (!sequence) {
                    co_return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "slice requires a sequence"
                    ));
                }
                auto target = type(source.type.construction(), source.origin);
                if (!target) {
                    co_return std::unexpected(target.error());
                }
                switch (operation.intrinsic) {
                    case SliceIntrinsic::Len:
                        co_return ConstantAtom {
                            .type = *target,
                            .value = IntegerConstant::from_parts(sequence->size(), false)
                        };
                    case SliceIntrinsic::IsEmpty:
                        co_return ConstantAtom {
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
                                auto operand =
                                    (co_await value(frame, operation.operands[index].expression));
                                if (!operand) {
                                    co_return std::unexpected(operand.error());
                                }
                                auto fact = read_fact(*operand, source.origin);
                                const auto* integer =
                                    fact ? std::get_if<IntegerConstant>(&fact->value) : nullptr;
                                if (!integer
                                    || integer->negative()
                                    || integer->magnitude() > sequence->size()) {
                                    co_return std::unexpected(fail(
                                        source.origin,
                                        DiagnosticCode::ConstIndexBounds,
                                        "slice range is out of bounds"
                                    ));
                                }
                                bounds.push_back(integer->magnitude());
                            }
                            if (bounds.size() != 2 || bounds[0] > bounds[1]) {
                                co_return std::unexpected(fail(
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
                            co_return std::unexpected(checked.error());
                        }
                        auto* aggregate = std::get_if<ExecutionAggregateValue>(&*receiver);
                        if (aggregate && operation.intrinsic == SliceIntrinsic::FromArray) {
                            aggregate->type = *target;
                            co_return std::move(*receiver);
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
                        co_return ExecutionAggregateValue {
                            .type = *target,
                            .elements = std::move(elements)
                        };
                    }
                }
                std::unreachable();
            } else if constexpr (std::same_as<Operation, SemRange>) {
                auto begin = (co_await value(frame, *operation.begin));
                if (!begin) {
                    co_return std::unexpected(begin.error());
                }
                auto end = (co_await value(frame, *operation.end));
                if (!end) {
                    co_return std::unexpected(end.error());
                }
                auto first = read_fact(*begin, source.origin);
                auto last = read_fact(*end, source.origin);
                if (!first || !last) {
                    co_return std::unexpected(!first ? first.error() : last.error());
                }
                auto range_type = type(source.type.construction(), source.origin);
                if (!range_type) {
                    co_return std::unexpected(range_type.error());
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
                    co_return std::unexpected(result_type.error());
                }
                if (auto checked = check_aggregate_size(*result_type, source.origin); !checked) {
                    co_return std::unexpected(checked.error());
                }
                if (auto checked = account_aggregate(operation.elements.size(), source.origin);
                    !checked) {
                    co_return std::unexpected(checked.error());
                }
                auto elements = std::vector<ExecutionValue>();
                elements.reserve(operation.elements.size());
                for (const auto& element : operation.elements) {
                    auto evaluated = (co_await value(frame, element));
                    if (!evaluated) {
                        co_return std::unexpected(evaluated.error());
                    }
                    elements.push_back(std::move(*evaluated));
                }
                co_return ExecutionAggregateValue {
                    .type = *result_type,
                    .elements = std::move(elements),
                };
            } else if constexpr (std::same_as<Operation, SemIndex>) {
                if (local_place(source)) {
                    auto selected = (co_await place(frame, source));
                    if (!selected) {
                        co_return std::unexpected(selected.error());
                    }
                    auto target = located(frame, *selected, source.origin);
                    if (!target) {
                        co_return std::unexpected(target.error());
                    }
                    co_return copy_value(**target, source.origin);
                }
                auto receiver = (co_await value(frame, *operation.source));
                if (!receiver) {
                    co_return std::unexpected(receiver.error());
                }
                auto subscript = (co_await value(frame, *operation.index));
                if (!subscript) {
                    co_return std::unexpected(subscript.error());
                }
                const auto sequence = execution_compound_view(values, *receiver);
                if (!sequence) {
                    co_return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstEvaluation,
                        "indexing requires an array value"
                    ));
                }
                auto position = offset(*subscript, sequence->size(), operation.index->origin);
                if (!position) {
                    co_return std::unexpected(position.error());
                }
                if (const auto* retained =
                        std::get_if<std::span<const ConstantID>>(&sequence->elements)) {
                    co_return ExecutionValue((*retained)[*position]);
                }
                co_return std::move(execution_elements(*receiver)[*position]);
            } else if constexpr (std::same_as<Operation, SemUnary>
                                 || std::same_as<Operation, SemCast>) {
                auto operand = (co_await value(frame, *operation.operand));
                if (!operand) {
                    co_return std::unexpected(operand.error());
                }
                if constexpr (std::same_as<Operation, SemCast>) {
                    if (operation.kind == CastKind::Identity) {
                        co_return std::move(*operand);
                    }
                }
                auto fact = read_fact(*operand, source.origin);
                auto target = type(source.type.construction(), source.origin);
                if (!fact || !target) {
                    co_return std::unexpected(!fact ? fact.error() : target.error());
                }
                if constexpr (std::same_as<Operation, SemUnary>) {
                    co_return finish(
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
                    co_return finish(
                        evaluate_cast_constant_value(values, operation.kind, *fact, *target),
                        source.origin
                    );
                }
            } else if constexpr (std::same_as<Operation, SemBinary>) {
                auto left = (co_await value(frame, *operation.left));
                if (!left) {
                    co_return std::unexpected(left.error());
                }
                auto right = (co_await value(frame, *operation.right));
                if (!right) {
                    co_return std::unexpected(right.error());
                }
                if (operation.operation == BinaryOperator::Equal
                    || operation.operation == BinaryOperator::NotEqual) {
                    auto target = type(source.type.construction(), source.origin);
                    if (!target) {
                        co_return std::unexpected(target.error());
                    }
                    const auto compared = equal(*left, *right, source.origin);
                    if (!compared) {
                        co_return std::unexpected(compared.error());
                    }
                    observe_test(
                        source,
                        *left,
                        &*right,
                        operation.operation == BinaryOperator::Equal ? *compared : !*compared
                    );
                    co_return ConstantAtom {
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
                    co_return std::unexpected(
                        !lhs       ? lhs.error()
                            : !rhs ? rhs.error()
                                   : target.error()
                    );
                }
                auto compared = finish(
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
                if (compared && test_observation && test_observation->condition == &source) {
                    const auto truth = boolean(*compared, source.origin);
                    if (truth) {
                        observe_test(source, *left, &*right, *truth);
                    }
                }
                co_return compared;
            } else if constexpr (std::same_as<Operation, SemShortCircuit>) {
                auto left = (co_await value(frame, *operation.left));
                if (!left) {
                    co_return std::unexpected(left.error());
                }
                auto truth = boolean(*left, operation.left->origin);
                if (!truth) {
                    co_return std::unexpected(truth.error());
                }
                const auto conjunction = operation.operation == ShortCircuitOperator::And;
                if (*truth != conjunction) {
                    observe_test(source, *left, nullptr, *truth);
                    co_return std::move(*left);
                }
                auto right = (co_await value(frame, *operation.right));
                if (right) {
                    const auto result = boolean(*right, source.origin);
                    if (result) {
                        observe_test(source, *left, &*right, *result);
                    }
                }
                co_return right;
            } else if constexpr (std::same_as<Operation, SemCall>) {
                const auto* callee = std::get_if<SemCallable>(&operation.callee->value);
                const auto function =
                    callee ? context.function_for_callable(callee->callable) : std::nullopt;
                if (!function) {
                    co_return std::unexpected(fail(
                        source.origin,
                        DiagnosticCode::ConstAdmission,
                        "execution requires a direct function call"
                    ));
                }
                auto operands = std::vector<ExecutionOperand>();
                for (const auto& argument : operation.arguments) {
                    auto evaluated = (co_await read_operand(frame, argument.expression));
                    if (!evaluated) {
                        co_return std::unexpected(evaluated.error());
                    }
                    operands.push_back(std::move(*evaluated));
                }
                auto arguments = std::vector<ExecutionValue>();
                for (auto& operand : operands) {
                    auto evaluated = materialize(frame, std::move(operand), source.origin);
                    if (!evaluated) {
                        co_return std::unexpected(evaluated.error());
                    }
                    arguments.push_back(std::move(*evaluated));
                }
                co_return (co_await invoke(*function, std::move(arguments), source.origin));
            } else if constexpr (std::same_as<Operation, SemPrint>) {
                co_return (co_await print(frame, operation, source.origin));
            } else if constexpr (std::same_as<Operation, SemTestReport>) {
                co_return (co_await test_report(frame, operation, source.origin));
            } else if constexpr (std::same_as<Operation, SemFormat>) {
                co_return (co_await format(frame, operation, source.origin));
            } else if constexpr (std::same_as<Operation, SemTextIntrinsic>) {
                auto target = type(source.type.construction(), source.origin);
                if (!target) {
                    co_return std::unexpected(target.error());
                }
                co_return (co_await text_intrinsic(frame, operation, *target, source.origin));
            } else {
                co_return std::unexpected(fail(
                    source.origin,
                    DiagnosticCode::ConstEvaluation,
                    "operation is not supported in execution"
                ));
            }
        }
    ));
    if (!result) {
        co_return std::unexpected(result.error());
    }
    co_return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = std::move(*result)};
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
                co_return std::unexpected(checked.error());
            }
            co_return (co_await pattern.value.visit(
                [&](const auto& pattern_value) noexcept -> ExecutionTask<bool> {
                    using Pattern = std::remove_cvref_t<decltype(pattern_value)>;
                    if constexpr (std::same_as<Pattern, WildcardPattern>) {
                        co_return true;
                    } else if constexpr (std::same_as<Pattern, BindingPattern>) {
                        auto copied = copy_value(subject, pattern.origin);
                        if (!copied) {
                            co_return std::unexpected(copied.error());
                        }
                        frame.slots[pattern_value.binding.index()] = std::move(*copied);
                        co_return true;
                    } else if constexpr (std::same_as<Pattern, TypeConstraintPattern>
                                         || std::
                                             same_as<Pattern, ElaboratedTypeConstraintPattern>) {
                        auto expected =
                            type(ConstructionTypeRef(pattern_value.type), pattern.origin);
                        if (!expected) {
                            co_return std::unexpected(expected.error());
                        }
                        co_return execution_value_type(values, subject) == *expected;
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
                                    DiagnosticCode::ConstEvaluation,
                                    "missing range bound"
                                ));
                            }
                            const auto& expression = upper ? found->end : found->begin;
                            auto result = (co_await value(frame, *expression));
                            if (!result) {
                                co_return std::unexpected(result.error());
                            }
                            co_return read_fact(*result, pattern.origin);
                        };
                        auto first = std::optional<ConstantFact>();
                        auto last = std::optional<ConstantFact>();
                        if (pattern_value.begin) {
                            auto result = (co_await read(*pattern_value.begin, false));
                            if (!result) {
                                co_return std::unexpected(result.error());
                            }
                            first = std::move(*result);
                        }
                        if (pattern_value.end) {
                            auto result = (co_await read(*pattern_value.end, true));
                            if (!result) {
                                co_return std::unexpected(result.error());
                            }
                            last = std::move(*result);
                        }
                        auto selected = read_fact(subject, pattern.origin);
                        if (!selected) {
                            co_return std::unexpected(selected.error());
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
                                    context.arithmetic()
                                ),
                                pattern.origin
                            );
                            if (!result) {
                                return std::unexpected(result.error());
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
                            DiagnosticCode::ConstEvaluation,
                            "pattern is not supported in execution"
                        ));
                    }
                }
            ));
        }
    ));
}
