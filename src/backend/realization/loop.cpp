module carven:backend.realization.loop.impl;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.task;
import std;

auto BodyRealizer::lower_loop(const SemLoop& value, LoweringStmtBuilder& destination) noexcept
    -> ContinuationTask<std::monostate> {
    auto initializer = (co_await region(*value.initializer, LoweringDiscardResult {}));
    if (!initializer.continues()) {
        destination.scope(std::move(initializer));
        co_return {};
    }
    auto condition_statements = LoweringStmtBuilder();
    auto condition = value.condition.has_value()
        ? condition_statements.accept((co_await this->condition(*value.condition)))
        : std::optional<LoweringPredicate>(LoweringKnownBool {true});
    if (!condition_statements.continues()) {
        initializer.append(std::move(condition_statements));
        destination.scope(std::move(initializer));
        co_return {};
    }
    if (known_predicate(condition) == false) {
        initializer.append(std::move(condition_statements));
        destination.scope(std::move(initializer));
        co_return {};
    }
    const auto step = value.steps->statements.empty()
        ? std::nullopt
        : std::optional(names.fresh(TargetTemporaryNameKind::Continue));
    const auto continuation = LoopContinuation {
        .step = step,
        .target = exit_target(LoweringExitKind::Continue),
        .break_target = exit_target(LoweringExitKind::Break)
    };
    const auto outer_loop = std::exchange(current_loop, continuation);
    auto body_statements = (co_await region(*value.body, LoweringDiscardResult {}));
    current_loop = outer_loop;
    const auto continued = body_statements.exits().contains(continuation.target);
    const auto breaks = body_statements.exits().contains(continuation.break_target);
    const auto run_steps = body_statements.continues() || continued;
    auto steps = run_steps ? (co_await region(*value.steps, LoweringDiscardResult {}))
                           : LoweringStmtBuilder();
    const auto conditional = known_predicate(condition) != true;
    auto loop_condition = bool_expression(true);
    const auto direct_condition = condition_statements.empty();
    auto iteration = std::move(condition_statements);
    if (direct_condition) {
        loop_condition = predicate_expression(std::move(*condition));
    } else if (conditional) {
        auto exit_body = LoweringStmtBuilder();
        exit_body.terminate(generated_statement(TargetBreakStmt {}), continuation.break_target);
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = prefix_expression(
                 TargetPrefixOperator::LogicalNot,
                 predicate_expression(std::move(*condition))
             ),
             .body = std::move(exit_body).finish()}
        );
        iteration.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    if (step.has_value()) {
        iteration.scope(std::move(body_statements));
    } else {
        iteration.append(std::move(body_statements));
    }
    if (continued && step.has_value()) {
        iteration.resume(*step, TargetJumpRole::ForLoopContinue, continuation.target);
    }
    iteration.append(std::move(steps));
    static_cast<void>(iteration.consume_exit(continuation.target));
    static_cast<void>(iteration.consume_exit(continuation.break_target));
    initializer.record_exits(iteration.exits());
    initializer.emit(
        generated_statement(
            TargetWhileStmt {
                .condition = std::move(loop_condition),
                .body = std::move(iteration).finish()
            }
        ),
        breaks || conditional
    );
    destination.scope(std::move(initializer));
    co_return {};
}

auto BodyRealizer::lower_range(const SemRangeLoop& value, LoweringStmtBuilder& destination) noexcept
    -> ContinuationTask<std::monostate> {
    auto scope = LoweringStmtBuilder();
    const auto index = fresh_local(TargetTemporaryNameKind::Operand);
    const auto range_value = std::holds_alternative<RangeTypeValue>(
        context.semantic().types().type(preparation.operation(value.source).type.resolved()).value
    );
    auto iterable = scope.accept((co_await operand(
        {.expression = std::addressof(value.source),
         .use = range_value                      ? PreparedUse::OperandValue
             : value.access == AccessMode::Write ? PreparedUse::WritePlace
                                                 : PreparedUse::ReadBorrow,
         .demand = PreparedDemand::Value}
    )));
    if (!scope.continues()) {
        destination.scope(std::move(scope));
        co_return {};
    }
    const auto continuation = LoopContinuation {
        .step = std::nullopt,
        .target = exit_target(LoweringExitKind::Continue),
        .break_target = exit_target(LoweringExitKind::Break)
    };
    const auto outer_loop = std::exchange(current_loop, continuation);
    auto iteration = (co_await region(*value.body, LoweringDiscardResult {}));
    current_loop = outer_loop;
    static_cast<void>(iteration.consume_exit(continuation.target));
    static_cast<void>(iteration.consume_exit(continuation.break_target));
    scope.record_exits(iteration.exits());
    scope.emit(generated_statement(
            TargetRangeForStmt {
                .binding = value.access == AccessMode::Write
                    ? TargetVariableBinding::MutableReference
                    : !value.binding.has_value() ? TargetVariableBinding::ConstReference
                                                 : TargetVariableBinding::MutableValue,
                .maybe_unused = true,
                .local = value.binding.has_value() ? binding_locals.at(*value.binding) : index,
                .type = value.binding.has_value()
                    ? (value.access == AccessMode::Write
                           ? context.intrinsic_type(TargetSymbol::Auto)
                           : context.lower_parameter(
                                 CallableParameter {
                                     .access = AccessMode::Read,
                                     .type = metadata.binding(*value.binding).type
                                 }
                             ))
                    : context.intrinsic_type(TargetSymbol::Auto),
                .range = range_value ? TargetExpr {.value = TargetConstructionExpr {
                    .type = context.lower_type(preparation.operation(value.source).type.resolved()),
                    .initializer = target_expressions(std::move(*iterable))}} : std::move(*iterable),
                .body = std::move(iteration).finish()
            }
        ));
    destination.scope(std::move(scope));
    co_return {};
}
