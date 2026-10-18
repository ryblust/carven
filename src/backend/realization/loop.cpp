module carven:backend.realization.loop.impl;

import :backend.generation.names;
import :backend.lowering.context;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.body;
import :semantic.semir.constant;
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
    if (steps.empty() || !steps.continues() || !steps.exits().entries.empty()) {
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
    const auto outer_scopes = async_scopes.size();
    auto initializer = (co_await region(*value.initializer, LoweringDiscardResult {}, true));
    if (!initializer.continues()) {
        leave_async_scopes(outer_scopes);
        destination.scope(std::move(initializer));
        co_return {};
    }
    auto condition_statements = LoweringStmtBuilder();
    auto condition = value.condition.has_value()
        ? condition_statements.accept((co_await this->condition(*value.condition)))
        : std::optional<LoweringPredicate>(LoweringKnownBool {true});
    if (!condition_statements.continues()) {
        initializer.append(std::move(condition_statements));
        leave_async_scopes(outer_scopes);
        destination.scope(std::move(initializer));
        co_return {};
    }
    if (known_predicate(condition) == false) {
        initializer.append(std::move(condition_statements));
        close_async_scopes(outer_scopes, false, initializer);
        leave_async_scopes(outer_scopes);
        destination.scope(std::move(initializer));
        co_return {};
    }
    // Lowering chooses syntax before realizing the body, so a native for loop
    // gives continue its C++ step semantics without a separate transfer target.
    const auto step_label = names.fresh(TargetTemporaryNameKind::Continue);
    const auto continuation = LoopContinuation {
        .step = step_label,
        .break_label = std::nullopt,
        .expanded = false,
        .target = exit_target(LoweringExitKind::Continue),
        .break_target = exit_target(LoweringExitKind::Break),
        .break_async_scopes = async_scopes.size(),
        .continue_async_scopes = async_scopes.size(),
    };
    const auto outer_loop = std::exchange(current_loop, continuation);
    auto steps = (co_await region(*value.steps, LoweringDiscardResult {}));
    const auto direct_condition = condition_statements.empty();
    auto for_step = direct_condition && !preparation.requires_coroutine_context(*value.steps)
        ? take_for_step(context, steps)
        : std::nullopt;
    const auto step = for_step || steps.empty() ? std::nullopt : std::optional(step_label);
    current_loop->step = step;
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
        iteration.emit(generated_statement(
            TargetIfStmt {
                .condition = prefix_expression(
                    TargetPrefixOperator::LogicalNot,
                    predicate_expression(std::move(*condition))
                ),
                .body = std::move(exit_body).finish(),
                .else_body = std::nullopt
            }
        ));
    }
    if (step.has_value()) {
        iteration.scope(std::move(body_statements));
    } else {
        iteration.append(std::move(body_statements));
    }
    if (continued && step.has_value()) {
        iteration.resume(*step, continuation.target);
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
    close_async_scopes(outer_scopes, false, initializer);
    leave_async_scopes(outer_scopes);
    destination.scope(std::move(initializer));
    co_return {};
}

auto BodyRealizer::lower_range(const SemRangeLoop& value, LoweringStmtBuilder& destination) noexcept
    -> ContinuationTask<std::monostate> {
    auto scope = LoweringStmtBuilder();
    const auto index = fresh_local(TargetTemporaryNameKind::Operand);
    const auto& source = preparation.operation(value.source);
    const auto* range =
        std::get_if<RangeTypeValue>(&context.semantic().types().type(source.type.resolved()).value);
    const auto range_value = range != nullptr;
    const auto borrowed = preparation.borrowed_array_elements(value);
    auto iterable = std::optional<TargetExpr>();
    if (!borrowed.empty()) {
        const auto element = context.lower_type(
            std::get<ArrayTypeValue>(context.semantic().types().type(source.type.resolved()).value)
                .element
        );
        const auto constant = context.target().intern_type(
            {.value =
                 TargetIntrinsicType {
                     .symbol = TargetSymbol::StdAddConst,
                     .type_argument_ids = {element}
                 },
             .const_qualified = false}
        );
        auto addresses = std::vector<TargetExpr>();
        for (const auto& source : borrowed) {
            const auto binding = std::get<SemBinding>(source.value).binding;
            addresses.push_back(call_expression(
                intrinsic_expression(TargetSymbol::StdAddressof),
                target_expressions(binding_expression(binding))
            ));
        }
        iterable = TargetExpr {
            .value = TargetArrayExpr {
                .element_type_id = context.pointer_type(constant),
                .extent = target_child(integer_expression(borrowed.size())),
                .elements = std::move(addresses)
            }
        };
    } else {
        iterable = scope.accept((co_await operand(
            {.expression = std::addressof(value.source),
             .use = range_value                      ? PreparedUse::OperandValue
                 : value.access == AccessMode::Write ? PreparedUse::WritePlace
                                                     : PreparedUse::ReadBorrow,
             .demand = PreparedDemand::Value}
        )));
    }
    if (!scope.continues()) {
        destination.scope(std::move(scope));
        co_return {};
    }
    const auto exclusive = [&]() noexcept {
        if (const auto* bounds = std::get_if<SemRange>(&source.value)) {
            return !bounds->inclusive;
        }
        if (source.constant) {
            const auto& constant = context.semantic().constants().constant(*source.constant);
            if (const auto* bounds = std::get_if<RangeConstant>(&constant.value)) {
                return !bounds->inclusive;
            }
        }
        return false;
    }();
    const auto bounds = range != nullptr && exclusive
        ? std::optional(fresh_local(TargetTemporaryNameKind::Operand))
        : std::nullopt;
    if (bounds) {
        scope.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = *bounds,
                .type = context.lower_type(source.type.resolved()),
                .initializer = std::move(*iterable)
            },
            false
        );
        scope.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = index,
                .type = context.lower_type(range->element),
                .initializer = member_expression(
                    name_expression(*bounds),
                    TargetIdentifier::from_spelling("first")
                )
            },
            false
        );
    }
    const auto continuation = LoopContinuation {
        .step = std::nullopt,
        .break_label = std::nullopt,
        .expanded = false,
        .target = exit_target(LoweringExitKind::Continue),
        .break_target = exit_target(LoweringExitKind::Break),
        .break_async_scopes = async_scopes.size(),
        .continue_async_scopes = async_scopes.size()
    };
    const auto outer_loop = std::exchange(current_loop, continuation);
    auto iteration = LoweringStmtBuilder();
    if (!borrowed.empty() && value.binding) {
        iteration.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstReference,
                .maybe_unused = true,
                .local = binding_locals.at(*value.binding),
                .type = context.lower_type(metadata.binding(*value.binding).type),
                .initializer = dereference_expression(name_expression(index))
            },
            false
        );
    }
    if (bounds && value.binding) {
        declare_binding(*value.binding, name_expression(index), iteration);
    }
    iteration.append((co_await region(*value.body, LoweringDiscardResult {})));
    current_loop = outer_loop;
    const auto advances = iteration.continues() || iteration.exits().contains(continuation.target);
    static_cast<void>(iteration.consume_exit(continuation.target));
    static_cast<void>(iteration.consume_exit(continuation.break_target));
    scope.record_exits(iteration.exits());
    if (bounds) {
        auto steps = std::vector<TargetForStep>();
        if (advances) {
            steps.push_back(
                TargetForStep {
                    .value = TargetUpdateStmt {
                        .op = TargetUpdateOperator::Increment,
                        .target = name_expression(index)
                    }
                }
            );
        }
        scope.emit(generated_statement(
            TargetForStmt {
                .initializer = std::nullopt,
                .condition = binary_expression(
                    name_expression(index),
                    TargetBinaryOperator::Less,
                    member_expression(
                        name_expression(*bounds),
                        TargetIdentifier::from_spelling("last")
                    )
                ),
                .steps = std::move(steps),
                .body = std::move(iteration).finish()
            }
        ));
    } else {
        scope.emit(generated_statement(
            TargetRangeForStmt {
                .binding = !borrowed.empty()            ? TargetVariableBinding::ConstValue
                    : value.access == AccessMode::Write ? TargetVariableBinding::MutableReference
                    : !value.binding.has_value()        ? TargetVariableBinding::ConstReference
                                                        : TargetVariableBinding::MutableValue,
                .maybe_unused = true,
                .local =
                    !borrowed.empty() || !value.binding ? index : binding_locals.at(*value.binding),
                .type = !borrowed.empty() ? context.intrinsic_type(TargetSymbol::Auto)
                    : value.binding.has_value()
                    ? (value.access == AccessMode::Write
                           ? context.intrinsic_type(TargetSymbol::Auto)
                           : context.lower_parameter(
                                 AccessMode::Read,
                                 metadata.binding(*value.binding).type,
                                 TypeNameScope::Module,
                                 context.semantic()
                                     .type_contents(metadata.binding(*value.binding).type)
                                     .contains_native_value
                             ))
                    : context.intrinsic_type(TargetSymbol::Auto),
                .range = std::move(*iterable),
                .body = std::move(iteration).finish()
            }
        ));
    }
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
            .expanded = true,
            .target = exit_target(LoweringExitKind::Continue),
            .break_target = break_target,
            .break_async_scopes = async_scopes.size(),
            .continue_async_scopes = async_scopes.size()
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
            iteration.resume(*step, continuation.target);
        }
        expanded.append(std::move(iteration));
    }
    if (expanded.exits().contains(break_target)) {
        expanded.resume(*break_label, break_target);
    }
    destination.scope(std::move(expanded));
    co_return {};
}
