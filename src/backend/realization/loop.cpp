module carven:backend.realization.loop.impl;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir;
import std;

auto BodyRealizer::lower_loop(
    ConstructionRegionID identity,
    const ConstructionLoop& value,
    LoweringStmtBuilder& destination
) noexcept -> void {
    auto initializer = region(value.initializer, LoweringDiscardResult {});
    if (!initializer.continues()) {
        destination.scope(std::move(initializer));
        return;
    }
    auto condition_statements = LoweringStmtBuilder();
    auto condition = value.condition.has_value()
        ? condition_statements.accept(this->condition(*value.condition))
        : std::optional<LoweringPredicate>(LoweringKnownBool {true});
    if (!condition_statements.continues()) {
        initializer.append(std::move(condition_statements));
        destination.scope(std::move(initializer));
        return;
    }
    if (known_predicate(condition) == false) {
        initializer.append(std::move(condition_statements));
        destination.scope(std::move(initializer));
        return;
    }
    const auto step = construction.region(value.steps).statements.empty()
        ? std::nullopt
        : std::optional(names.fresh(TargetTemporaryNameKind::Continue));
    loops.emplace(
        identity,
        LoopContinuation {
            .step = step,
            .target = exit_target(LoweringExitKind::Continue),
            .break_target = exit_target(LoweringExitKind::Break)
        }
    );
    auto body_statements = region(value.body, LoweringDiscardResult {});
    const auto continuation = loops.at(identity);
    const auto continued = body_statements.exits().contains(continuation.target);
    const auto breaks = body_statements.exits().contains(continuation.break_target);
    const auto run_steps = body_statements.continues() || continued;
    auto steps = run_steps ? region(value.steps, LoweringDiscardResult {}) : LoweringStmtBuilder();
    auto iteration = std::move(condition_statements);
    if (known_predicate(condition) != true) {
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
                .condition = bool_expression(true),
                .body = std::move(iteration).finish()
            }
        ),
        breaks || (known_predicate(condition) != true)
    );
    destination.scope(std::move(initializer));
}

auto BodyRealizer::lower_range(
    ConstructionRegionID identity,
    const ConstructionRangeLoop& value,
    LoweringStmtBuilder& destination
) noexcept -> void {
    const auto* integer = std::get_if<ConstructionIntegerRange>(&value.source);
    const auto& first =
        integer ? integer->begin : std::get<ConstructionSequenceRange>(value.source).value;

    auto scope = LoweringStmtBuilder();
    const auto index = names.fresh(TargetTemporaryNameKind::Operand);
    auto begin = scope.accept(operand({
        .expression = first,
        .use = integer != nullptr               ? ConstructionUse::OperandValue
            : value.access == AccessMode::Write ? ConstructionUse::Place
                                                : ConstructionUse::ReadBorrow,
    }));
    if (!scope.continues()) {
        destination.scope(std::move(scope));
        return;
    }
    if (integer == nullptr) {
        loops.emplace(
            identity,
            LoopContinuation {
                .step = std::nullopt,
                .target = exit_target(LoweringExitKind::Continue),
                .break_target = exit_target(LoweringExitKind::Break)
            }
        );
        auto iteration = region(value.body, LoweringDiscardResult {});
        const auto continuation = loops.at(identity);
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
                .name = value.binding.has_value() ? binding_names.at(*value.binding) : index,
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
                .range = std::move(*begin),
                .body = std::move(iteration).finish()
            }
        ));
        destination.scope(std::move(scope));
        return;
    }
    const auto index_type = context.lower_type(construction.expression(first).type);
    const auto retain_bound = [&](ConstructionExpressionID id, TargetExpr expression) noexcept {
        const auto& source = construction.expression(id);
        if (!source.reads_storage && !source.requires_execution) {
            return expression;
        }
        const auto name = names.fresh(TargetTemporaryNameKind::Operand);
        scope.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .name = name,
                .type = index_type,
                .initializer = std::move(expression)
            }
        ));
        return name_expression(name);
    };
    auto initial = retain_bound(first, std::move(*begin));
    auto upper = read_value(expression(integer->end), scope);
    if (!scope.continues()) {
        destination.scope(std::move(scope));
        return;
    }
    auto limit = retain_bound(integer->end, std::move(*upper));
    auto element = name_expression(index);
    loops.emplace(
        identity,
        LoopContinuation {
            .step = std::nullopt,
            .target = exit_target(LoweringExitKind::Continue),
            .break_target = exit_target(LoweringExitKind::Break)
        }
    );
    auto iteration = LoweringStmtBuilder();
    if (value.binding.has_value()) {
        const auto id = *value.binding;
        iteration.emit(generated_statement(
            TargetVariableStmt {
                .binding = value.access == AccessMode::Write
                    ? TargetVariableBinding::MutableReference
                    : TargetVariableBinding::ConstValue,
                .maybe_unused = true,
                .name = binding_names.at(id),
                .type = context.lower_type(metadata.binding(id).type),
                .initializer = std::move(element)
            }
        ));
    }
    iteration.append(region(value.body, LoweringDiscardResult {}));
    const auto continuation = loops.at(identity);
    static_cast<void>(iteration.consume_exit(continuation.target));
    static_cast<void>(iteration.consume_exit(continuation.break_target));
    scope.record_exits(iteration.exits());
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
                std::move(limit)
            ),
            .steps = std::move(steps),
            .body = std::move(iteration).finish(),
        }
    ));
    destination.scope(std::move(scope));
}
