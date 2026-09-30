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
import :support.invariant;
import :support.task;
import std;

namespace {

auto take_for_step(ModuleLowering& context, LoweringStmtBuilder& steps) noexcept
    -> std::optional<TargetForStep> {
    if (steps.empty() || !steps.continues() || !steps.exits().targets.empty()) {
        return std::nullopt;
    }
    auto statements = std::move(steps).finish();
    steps = LoweringStmtBuilder();
    if (statements.size() == 1uz) {
        auto step = statements.front().value.visit(
            [](auto& value) static noexcept -> std::optional<TargetForStep> {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, TargetExprStmt>
                              || std::same_as<Value, TargetDiscardStmt>
                              || std::same_as<Value, TargetAssignmentStmt>
                              || std::same_as<Value, TargetUpdateStmt>) {
                    return TargetForStep {.value = std::move(value)};
                } else {
                    return std::nullopt;
                }
            }
        );
        if (step) {
            return step;
        }
    }
    // The lambda closes the step region without merging its statements into a
    // comma full-expression. Local owners keep their existing cleanup scopes;
    // outward returns, failures and test stops were excluded by the exit summary.
    return TargetForStep {
        .value = TargetExprStmt {
            .expression = call_expression(
                TargetExpr {
                    .value =
                        TargetLambdaExpr {
                            .parameters = {},
                            .result = context.intrinsic_type(TargetSymbol::Void),
                            .body = std::move(statements)
                        }
                },
                {}
            )
        }
    };
}

} // namespace

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
    // Lowering chooses syntax before realizing the body, so a native for loop
    // gives continue its C++ step semantics without a separate transfer target.
    auto steps = (co_await region(*value.steps, LoweringDiscardResult {}));
    const auto direct_condition = condition_statements.empty();
    auto for_step = direct_condition ? take_for_step(context, steps) : std::nullopt;
    const auto step = for_step || steps.empty()
        ? std::nullopt
        : std::optional(names.fresh(TargetTemporaryNameKind::Continue));
    const auto continuation = LoopContinuation {
        .step = step,
        .break_label = std::nullopt,
        .jump_role = TargetJumpRole::ForLoopContinue,
        .expanded = false,
        .target = exit_target(LoweringExitKind::Continue),
        .break_target = exit_target(LoweringExitKind::Break)
    };
    const auto outer_loop = std::exchange(current_loop, continuation);
    auto body_statements = (co_await region(*value.body, LoweringDiscardResult {}));
    current_loop = outer_loop;
    const auto continued = body_statements.exits().contains(continuation.target);
    const auto breaks = body_statements.exits().contains(continuation.break_target);
    const auto run_steps = body_statements.continues() || continued;
    if (!run_steps) {
        steps = LoweringStmtBuilder();
        for_step.reset();
    }
    const auto conditional = known_predicate(condition) != true;
    auto loop_condition = bool_expression(true);
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
    if (for_step) {
        auto header_steps = std::vector<TargetForStep>();
        header_steps.push_back(std::move(*for_step));
        initializer.emit(
            generated_statement(
                TargetForStmt {
                    .initializer = std::nullopt,
                    .condition = std::move(loop_condition),
                    .steps = std::move(header_steps),
                    .body = std::move(iteration).finish(),
                }
            ),
            breaks || conditional
        );
    } else {
        initializer.emit(
            generated_statement(
                TargetWhileStmt {
                    .condition = std::move(loop_condition),
                    .body = std::move(iteration).finish(),
                }
            ),
            breaks || conditional
        );
    }
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
        .break_label = std::nullopt,
        .jump_role = TargetJumpRole::ForLoopContinue,
        .expanded = false,
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
            .binding = value.access == AccessMode::Write ? TargetVariableBinding::MutableReference
                : !value.binding.has_value()             ? TargetVariableBinding::ConstReference
                                                         : TargetVariableBinding::MutableValue,
            .maybe_unused = true,
            .local = value.binding.has_value() ? binding_locals.at(*value.binding) : index,
            .type = value.binding.has_value()
                ? (value.access == AccessMode::Write ? context.intrinsic_type(TargetSymbol::Auto)
                                                     : context.lower_parameter(
                                                           AccessMode::Read,
                                                           metadata.binding(*value.binding).type
                                                       ))
                : context.intrinsic_type(TargetSymbol::Auto),
            .range = std::move(*iterable),
            .body = std::move(iteration).finish()
        }
    ));
    destination.scope(std::move(scope));
    co_return {};
}

auto BodyRealizer::lower_expanded_loop(
    const SemExpandedLoop& value,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    auto expanded = LoweringStmtBuilder();
    const auto break_target = exit_target(LoweringExitKind::Break);
    auto break_label = std::optional<TargetIdentifier>();
    for (const auto& source : value.iterations) {
        if (!expanded.continues()) {
            break;
        }
        const auto continuation = LoopContinuation {
            .step = std::nullopt,
            .break_label = break_label,
            .jump_role = TargetJumpRole::RegionExit,
            .expanded = true,
            .target = exit_target(LoweringExitKind::Continue),
            .break_target = break_target
        };
        const auto outer_loop = std::exchange(current_loop, continuation);
        auto body = (co_await region(source, LoweringDiscardResult {}));
        const auto step = current_loop->step;
        break_label = current_loop->break_label;
        current_loop = outer_loop;
        const auto continued = body.exits().contains(continuation.target);
        auto iteration = LoweringStmtBuilder();
        iteration.scope(std::move(body));
        if (continued) {
            iteration.resume(*step, TargetJumpRole::RegionExit, continuation.target);
        }
        expanded.append(std::move(iteration));
    }
    if (expanded.exits().contains(break_target)) {
        expanded.resume(*break_label, TargetJumpRole::RegionExit, break_target);
    }
    destination.scope(std::move(expanded));
    co_return {};
}
