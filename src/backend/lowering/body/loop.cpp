module carven:backend.lowering.body.loop.impl;

import :backend.generation.names;
import :backend.lowering.body.lowerer;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import std;

namespace body_lowering {
auto BodyLowerer::lower_loop(const SemLoop& value, StatementSequence& destination) noexcept
    -> void {
    auto initializer =
        region(*value.initializer, {.use = ResultUse::Discard, .storage = std::nullopt});
    if (!initializer.continues()) {
        destination.block(std::move(initializer));
        return;
    }
    auto condition_statements = StatementSequence();
    auto condition = value.condition.has_value()
        ? this->condition(*value.condition, condition_statements)
        : bool_expression(true);
    if (!condition_statements.continues()) {
        initializer.append(std::move(condition_statements));
        destination.block(std::move(initializer));
        return;
    }
    if (value.condition.has_value() && known_boolean(*value.condition) == false) {
        initializer.append(std::move(condition_statements));
        initializer.emit(
            generated_statement(TargetDiscardStmt {.expression = std::move(*condition)})
        );
        destination.block(std::move(initializer));
        return;
    }
    const auto step = value.steps->statements.empty()
        ? std::nullopt
        : std::optional(names.fresh(TargetTemporaryNameKind::Continue));
    const auto previous = std::exchange(loop_continuation, {.step = step});
    auto body_statements =
        region(*value.body, {.use = ResultUse::Discard, .storage = std::nullopt});
    const auto continuation = std::exchange(loop_continuation, previous);
    const auto run_steps = body_statements.continues() || continuation.used;
    auto steps = run_steps
        ? region(*value.steps, {.use = ResultUse::Discard, .storage = std::nullopt})
        : StatementSequence();
    auto iteration = std::move(condition_statements);
    if (value.condition.has_value() && known_boolean(*value.condition) != true) {
        auto exit_body = StatementSequence();
        exit_body.terminate(generated_statement(TargetBreakStmt {}));
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition =
                 prefix_expression(TargetPrefixOperator::LogicalNot, std::move(*condition)),
             .body = std::move(exit_body).finish()}
        );
        iteration.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    } else if (value.condition.has_value()) {
        iteration.emit(
            generated_statement(TargetDiscardStmt {.expression = std::move(*condition)})
        );
    }
    iteration.block(std::move(body_statements));
    if (continuation.used && step.has_value()) {
        iteration.resume(*step, TargetJumpRole::ForLoopContinue);
    }
    iteration.append(std::move(steps));
    initializer.emit(
        generated_statement(
            TargetWhileStmt {
                .condition = bool_expression(true),
                .body = std::move(iteration).finish()
            }
        ),
        continuation.breaks
            || (value.condition.has_value() && known_boolean(*value.condition) != true)
    );
    destination.block(std::move(initializer));
}

auto BodyLowerer::lower_range(const SemRangeLoop& value, StatementSequence& destination) noexcept
    -> void {
    auto scope = StatementSequence();
    const auto index = names.fresh(TargetTemporaryNameKind::Operand);
    const auto limit = names.fresh(TargetTemporaryNameKind::Operand);
    const auto owner = names.fresh(TargetTemporaryNameKind::Owner);
    auto begin = value.end.has_value() ? operand(value.begin, scope, OperandUse::Snapshot)
                                       : expression(value.begin, scope);
    if (!scope.continues()) {
        destination.block(std::move(scope));
        return;
    }
    if (!value.end.has_value()) {
        scope.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::RvalueReference,
                .maybe_unused = false,
                .name = owner,
                .type = context.intrinsic_type(TargetSymbol::Auto),
                .initializer = std::move(*begin)
            }
        ));
        const auto previous = std::exchange(loop_continuation, {});
        auto iteration = region(*value.body, {.use = ResultUse::Discard, .storage = std::nullopt});
        loop_continuation = previous;
        scope.emit(generated_statement(
            TargetRangeForStmt {
                .binding = value.access == AccessMode::Write
                    ? TargetVariableBinding::MutableReference
                    : !value.binding.has_value() ? TargetVariableBinding::ConstReference
                                                 : TargetVariableBinding::MutableValue,
                .maybe_unused = true,
                .name = value.binding.has_value() ? binding_names.at(*value.binding) : index,
                .type = value.binding.has_value()
                    ? (value.access == AccessMode::Write
                           ? context.lower_type(body.binding(*value.binding).type)
                           : context.lower_parameter(
                                 CallableParameter {
                                     .access = AccessMode::Read,
                                     .type = body.binding(*value.binding).type
                                 }
                             ))
                    : context.intrinsic_type(TargetSymbol::Auto),
                .range = name_expression(owner),
                .body = std::move(iteration).finish()
            }
        ));
        destination.block(std::move(scope));
        return;
    }
    auto initial = std::move(*begin);
    const auto index_type = context.lower_type(value.begin.type.resolved());
    auto upper = expression(*value.end, scope);
    if (!scope.continues()) {
        destination.block(std::move(scope));
        return;
    }
    auto element = name_expression(index);
    scope.emit(generated_statement(
        TargetVariableStmt {
            .binding = TargetVariableBinding::ConstValue,
            .maybe_unused = false,
            .name = limit,
            .type = index_type,
            .initializer = std::move(*upper)
        }
    ));
    const auto previous = std::exchange(loop_continuation, {});
    auto iteration = StatementSequence();
    if (value.binding.has_value()) {
        const auto id = *value.binding;
        iteration.emit(generated_statement(
            TargetVariableStmt {
                .binding = value.access == AccessMode::Write
                    ? TargetVariableBinding::MutableReference
                    : TargetVariableBinding::ConstValue,
                .maybe_unused = true,
                .name = binding_names.at(id),
                .type = context.lower_type(body.binding(id).type),
                .initializer = std::move(element)
            }
        ));
    }
    iteration.append(region(*value.body, {.use = ResultUse::Discard, .storage = std::nullopt}));
    loop_continuation = previous;
    auto steps = std::vector<TargetForStep>();
    steps.push_back(
        {.value = TargetUpdateStmt {
             .op = TargetUpdateOperator::Increment,
             .target = name_expression(index)
         }}
    );
    scope.emit(generated_statement(
        TargetForStmt {
            .initializer =
                TargetForInitializer {
                    .value =
                        TargetVariableStmt {
                            .binding = TargetVariableBinding::MutableValue,
                            .maybe_unused = false,
                            .name = index,
                            .type = index_type,
                            .initializer = std::move(initial)
                        }
                },
            .condition = binary_expression(
                name_expression(index),
                TargetBinaryOperator::Less,
                name_expression(limit)
            ),
            .steps = std::move(steps),
            .body = std::move(iteration).finish(),
        }
    ));
    destination.block(std::move(scope));
}

} // namespace body_lowering
