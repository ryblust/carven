module carven:backend.realization.stmt.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.provenance;
import :support.invariant;
import :support.task;
import :support.visit;
import std;

auto BodyRealizer::emit_return(
    std::optional<TargetExpr> value,
    LoweringStmtBuilder& destination,
    const LoweringResultDestination& result
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (std::holds_alternative<LoweringReturnResult>(result)) {
        if (const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit)) {
            const auto& signature = context.semantic().callable_signatures().signature(
                context.semantic().declarations().callable(callable->callable_id).signature
            );
            if (!context.plan().failure_abi().members(signature.failures).empty()
                || context.semantic().may_stop_test(callable->callable_id)) {
                auto arguments = std::vector<TargetExpr>();
                if (value.has_value()) {
                    if (context.is_void(signature.result)) {
                        destination.emit(statement_expression(std::move(*value)));
                    } else {
                        auto body = std::vector<TargetStmt>();
                        body.push_back(
                            generated_statement(TargetReturnStmt {.expression = std::move(*value)})
                        );
                        arguments.push_back(
                            TargetExpr {
                                .value = TargetLambdaExpr {
                                    .parameters = {},
                                    .result = context.lower_type(signature.result),
                                    .body = std::move(body)
                                }
                            }
                        );
                    }
                }
                value = call_expression(
                    static_member_expression(
                        context.callable_result(callable->callable_id),
                        TargetIdentifier::from_spelling(
                            context.is_void(signature.result) ? "success" : "success_from"
                        )
                    ),
                    std::move(arguments)
                );
            }
        }
    }
    destination.terminate(
        generated_statement(TargetReturnStmt {.expression = std::move(value)}),
        std::holds_alternative<LoweringYieldResult>(result)
            ? std::get<LoweringYieldResult>(result).target
            : LoweringExitTarget {LoweringExitKind::FunctionReturn, 0}
    );
}

auto BodyRealizer::emit_failure(
    TargetExpr value,
    const std::optional<FailureDestination>& exit,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (exit) {
        const auto& failure_destination = *exit;
        destination.emit(statement_expression(call_member(
            name_expression(failure_destination.slot.storage),
            "emplace",
            target_expressions(std::move(value))
        )));
        destination.terminate(
            generated_statement(
                TargetGotoStmt {
                    .label = failure_destination.label,
                    .role = TargetJumpRole::FailureTransfer
                }
            ),
            failure_destination.target
        );
        return;
    }
    const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
    if (callable == nullptr) {
        invariant_violation("failure escaped a test body");
    }
    destination.terminate(
        generated_statement(
            TargetReturnStmt {
                .expression = call_expression(
                    static_member_expression(
                        context.callable_result(callable->callable_id),
                        TargetIdentifier::from_spelling("failure")
                    ),
                    target_expressions(std::move(value))
                )
            }
        ),
        LoweringExitTarget {LoweringExitKind::Failure, 0}
    );
}

auto BodyRealizer::transfer_failure(
    FailureSlot slot,
    FailureSetID failures,
    const std::optional<FailureDestination>& exit,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    destination.scope(dispatch_failure(slot, failures, exit));
}

auto BodyRealizer::failure_projection(FailureSlot slot, TypeID type) noexcept -> TargetExpr {
    auto payload = address_expression(dereference_expression(name_expression(slot.storage)));
    if (context.plan().failure_abi().members(slot.layout).size() == 1uz) {
        return payload;
    }
    return template_call_expression(
        intrinsic_expression(TargetSymbol::StdGetIf),
        {context.lower_type(type)},
        target_expressions(std::move(payload))
    );
}

auto BodyRealizer::dispatch_failure(
    const FailureSource& source,
    FailureSetID failures,
    const std::optional<FailureDestination>& exit
) noexcept -> LoweringStmtBuilder {
    auto transfers = LoweringStmtBuilder();
    const auto candidates = context.plan().failure_abi().members(failures);
    for (const auto type : candidates) {
        const auto projection = fresh_local(TargetTemporaryNameKind::FailureProjection);
        transfers.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = projection,
                .type = context.pointer_type(context.intrinsic_type(TargetSymbol::Auto)),
                .initializer = source.visit(
                    Overloaded {
                        [&](const OutcomeFailureSource& outcome) noexcept {
                            auto storage = name_expression(outcome.storage);
                            if (outcome.deferred) {
                                storage = dereference_expression(std::move(storage));
                            }
                            return template_call_expression(
                                member_expression(
                                    std::move(storage),
                                    TargetIdentifier::from_spelling("failure_if")
                                ),
                                {context.lower_type(type)},
                                {}
                            );
                        },
                        [&](const FailureSlot& slot) noexcept {
                            return failure_projection(slot, type);
                        }
                    }
                )
            }
        ));
        auto transfer = LoweringStmtBuilder();
        emit_failure(
            transfer_expression(dereference_expression(name_expression(projection))),
            exit,
            transfer
        );
        if (type == candidates.back()) {
            transfers.append(std::move(transfer));
            return transfers;
        }
        transfers.record_exits(transfer.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = name_expression(projection), .body = std::move(transfer).finish()}
        );
        transfers.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    transfers.terminate(
        generated_statement(TargetUnreachableStmt {.reason = TargetUnreachableReason::SemIRProof}),
        LoweringExitTarget {LoweringExitKind::Unreachable, 0}
    );
    return transfers;
}

auto BodyRealizer::result_expression(
    const SemanticExpression& source,
    const LoweringResultDestination& result,
    LoweringStmtBuilder& destination,
    std::optional<LifetimeRegionID> delivered_region
) noexcept -> ContinuationTask<std::monostate> {
    if (!destination.continues()) {
        co_return {};
    }
    if (std::holds_alternative<SemIf>(preparation.operation(source).value)
        || std::holds_alternative<SemMatch>(preparation.operation(source).value)
        || std::holds_alternative<SemTry>(preparation.operation(source).value)) {
        (co_await structured_delivery(source, result, destination));
        co_return {};
    }
    if (std::holds_alternative<LoweringDiscardResult>(result)) {
        if (preparation.summary(source).requires_execution) {
            static_cast<void>(destination.accept((co_await expression(
                source,
                ConstantLiteralContext::Exact,
                ResultDemand::Discard,
                delivered_region
            ))));
        }
        co_return {};
    }
    const auto& expression_source = preparation.operation(source);
    const auto* call = std::get_if<SemCall>(&expression_source.value);
    const auto transport = fallible(expression_source);
    const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
    if (std::holds_alternative<LoweringReturnResult>(result)
        && callable != nullptr
        && transport
        && !transport->destination
        && call != nullptr
        && context.call_result(*call) == context.callable_result(callable->callable_id)) {
        auto value = destination.accept((co_await expression(
            source,
            ConstantLiteralContext::Exact,
            ResultDemand::PropagateOutcome,
            delivered_region
        )));
        if (value) {
            if (!context.plan().failure_abi().members(transport->failures).empty()) {
                destination.record_exits(
                    LoweringExitSummary {.targets = {{LoweringExitKind::Failure, 0}}}
                );
            }
            destination.terminate(
                generated_statement(
                    TargetReturnStmt {.expression = require_expression(std::move(*value))}
                ),
                LoweringExitTarget {LoweringExitKind::FunctionReturn, 0}
            );
        }
        co_return {};
    }
    const auto native_result = callable != nullptr
        && context.plan()
               .failure_abi()
               .members(context.semantic()
                            .callable_signatures()
                            .signature(context.semantic()
                                           .declarations()
                                           .callable(callable->callable_id)
                                           .signature)
                            .failures)
               .empty();
    auto literal = ConstantLiteralContext::Exact;
    if (returns_result(result)
        && (std::holds_alternative<LoweringYieldResult>(result) || native_result)) {
        literal = ConstantLiteralContext::TargetTyped;
    }
    auto demand = ResultDemand::Value;
    if (std::holds_alternative<LoweringReturnResult>(result)
        && callable != nullptr
        && native_result
        && !context.semantic().may_stop_test(callable->callable_id)) {
        demand = ResultDemand::DirectReturn;
    }
    auto value =
        destination.accept((co_await expression(source, literal, demand, delivered_region)));
    if (value) {
        deliver_result(std::move(*value), result, destination);
    }
    co_return {};
}

auto BodyRealizer::statement(const SemanticStatement& source) noexcept
    -> ContinuationTask<Lowered<LoweringCompleted>> {
    auto destination = LoweringStmtBuilder();
    co_await source.value.visit(
        Overloaded {
            [&](const SemReturn& value) noexcept -> ContinuationTask<std::monostate> {
                if (value.value) {
                    (co_await result_expression(
                        *value.value,
                        LoweringReturnResult {},
                        destination
                    ));
                } else {
                    emit_return(std::nullopt, destination);
                }
                co_return {};
            },
            [&]<typename Transfer>(const Transfer&) noexcept -> ContinuationTask<std::monostate>
                requires (std::same_as<Transfer, SemBreak> || std::same_as<Transfer, SemContinue>)
            {
                if (!current_loop) {
                    invariant_violation("loop transfer has no target");
                }
                const auto& loop = *current_loop;
                if constexpr (std::same_as<Transfer, SemBreak>) {
                    destination.terminate(
                        generated_statement(TargetBreakStmt {}),
                        loop.break_target
                    );
                } else if (loop.step) {
                    destination.terminate(
                        generated_statement(
                            TargetGotoStmt {
                                .label = *loop.step,
                                .role = TargetJumpRole::ForLoopContinue
                            }
                        ),
                        loop.target
                    );
                } else {
                    destination.terminate(generated_statement(TargetContinueStmt {}), loop.target);
                }
                co_return {};
            },
            [&](const SemRethrow&) noexcept -> ContinuationTask<std::monostate> {
                transfer_failure(caught->slot, caught->failures, current_failure, destination);
                co_return {};
            },
            [&](const SemThrow& value) noexcept -> ContinuationTask<std::monostate> {
                auto failure = read_value((co_await expression(value.value)), destination);
                if (failure) {
                    emit_failure(std::move(*failure), current_failure, destination);
                }
                co_return {};
            },
            [&](const SemExpressionStatement& value) noexcept -> ContinuationTask<std::monostate> {
                auto evaluation = LoweringStmtBuilder();
                (co_await result_expression(
                    value.expression,
                    LoweringDiscardResult {},
                    evaluation
                ));
                destination.scope(std::move(evaluation));
                co_return {};
            },
            [&](const SemInitialize& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await initialize_binding(value, destination));
                co_return {};
            },
            [&](const SemAssign& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await assign(value, destination));
                co_return {};
            },
            [&](const OwnedSemanticRegion& value) noexcept -> ContinuationTask<std::monostate> {
                destination.scope((co_await region(*value, LoweringDiscardResult {})));
                co_return {};
            },
            [&](const SemLoop& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await lower_loop(value, destination));
                co_return {};
            },
            [&](const SemRangeLoop& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await lower_range(value, destination));
                co_return {};
            },
            }
    );
    destination.attribute(
        TargetSourceExpansionAttribution {
            .origin = target_source_origin(context.semantic().provenance(), source.origin)
        }
    );
    const auto normal =
        destination.continues() ? std::optional(LoweringCompleted {}) : std::nullopt;
    co_return std::move(destination).complete<LoweringCompleted>(normal);
}
