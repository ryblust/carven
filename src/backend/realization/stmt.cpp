module carven:backend.realization.stmt.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.decl;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.stage;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.provenance;
import :source.provenance.ids;
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
    if (local_result_exit && std::holds_alternative<LoweringReturnResult>(result)) {
        if (std::holds_alternative<LoweringDiscardResult>(local_result_exit->result)) {
            if (value) {
                destination.emit(statement_expression(std::move(*value)));
            }
        } else {
            deliver_result(
                value ? LoweringResult(LoweringDirectExpression {std::move(*value)})
                      : LoweringResult(LoweringCompleted {}),
                local_result_exit->result,
                destination
            );
        }
        destination.terminate(
            generated_statement(
                TargetGotoStmt {
                    .label = local_result_exit->exit.label,
                    .role = TargetJumpRole::RegionExit
                }
            ),
            local_result_exit->exit.target
        );
        return;
    }
    const auto async_return = is_async() && std::holds_alternative<LoweringReturnResult>(result);
    if (std::holds_alternative<LoweringReturnResult>(result)) {
        if (const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit)) {
            const auto& signature = context.semantic().callable_signatures().signature(
                context.semantic().declarations().callable(callable->callable_id).signature
            );
            if (async_return
                || !context.plan().failure_abi().members(signature.failures).empty()
                || context.semantic().may_stop_test(callable->callable_id)) {
                auto arguments = std::vector<TargetExpr>();
                const auto& type = context.semantic().types().type(signature.result).value;
                const auto* builtin = std::get_if<BuiltinTypeValue>(&type);
                // Scalar results pass by value; others construct in place.
                const auto direct = context.is_void(signature.result)
                    || (builtin != nullptr && builtin->kind != BuiltinType::String)
                    || std::holds_alternative<PointerTypeValue>(type);
                if (value.has_value()) {
                    if (context.is_void(signature.result)) {
                        destination.emit(statement_expression(std::move(*value)));
                    } else if (direct) {
                        arguments.push_back(std::move(*value));
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
                        async_return ? completion_type()
                                     : context.callable_result(callable->callable_id),
                        TargetIdentifier::from_spelling(direct ? "success" : "success_from")
                    ),
                    std::move(arguments)
                );
            }
        }
    }
    if (async_return) {
        if (!value) {
            invariant_violation("async return did not construct Completion");
        }
        emit_completion(std::move(*value), LoweringExitKind::FunctionReturn, destination);
        return;
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
    const SemanticExpression& source,
    const std::optional<FailureDestination>& exit,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (exit) {
        const auto storage = fresh_local(TargetTemporaryNameKind::Try);
        unused_initializers.emplace(
            storage,
            preparation.summary(source).requires_execution ? UnusedInitializer::Evaluate
                                                           : UnusedInitializer::Omit
        );
        register_failure(
            ValueFailureSource {
                .storage = storage,
                .type = preparation.operation(source).type.resolved()
            },
            failure_receivers[exit->identity].layout,
            *exit,
            destination,
            std::move(value)
        );
        return;
    }
    deliver_failure(std::move(value), std::nullopt, destination);
}

auto BodyRealizer::deliver_failure(
    TargetExpr value,
    const std::optional<FailureRelay>& relay,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (relay) {
        destination.emit(statement_expression(call_member(
            name_expression(relay->slot.storage),
            "emplace",
            target_expressions(std::move(value))
        )));
        emit_async_exit(
            generated_statement(
                TargetGotoStmt {.label = relay->label, .role = TargetJumpRole::FailureTransfer}
            ),
            relay->target,
            relay->closing_scopes,
            true,
            destination
        );
        return;
    }
    const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
    if (callable == nullptr) {
        invariant_violation("failure escaped a test body");
    }
    if (is_async()) {
        emit_completion(
            call_expression(
                static_member_expression(
                    completion_type(),
                    TargetIdentifier::from_spelling("failure")
                ),
                target_expressions(std::move(value))
            ),
            LoweringExitKind::Failure,
            destination
        );
        return;
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
    const FailureSource& source,
    FailureSetID failures,
    const std::optional<FailureDestination>& exit,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!destination.continues()) {
        return;
    }
    if (exit
        && std::ranges::any_of(
            context.plan().failure_abi().members(failures),
            [&](TypeID type) noexcept { return failure_contains(source, type); }
        )) {
        register_failure(source, failures, *exit, destination);
        return;
    }
    destination.scope(dispatch_failure(source, failures, std::nullopt));
}

auto BodyRealizer::register_failure(
    FailureSource source,
    FailureSetID failures,
    const FailureDestination& receiver,
    LoweringStmtBuilder& destination,
    std::optional<TargetExpr> initializer
) noexcept -> void {
    const auto label = names.fresh(TargetTemporaryNameKind::Try);
    failure_receivers[receiver.identity].edges.push_back(
        {.label = label,
         .source = source,
         .failures = failures,
         .initializer = std::move(initializer),
         .closing_scopes = async_closing_locals(receiver.retained_async_scopes)}
    );
    // This registered identity is completed before the protected region leaves lowering.
    destination.terminate(
        generated_statement(
            TargetGotoStmt {.label = label, .role = TargetJumpRole::FailureTransfer}
        ),
        receiver.target
    );
}

auto BodyRealizer::failure_contains(const FailureSource& source, TypeID type) const noexcept
    -> bool {
    return source.visit(
        Overloaded {
            [&](const ValueFailureSource& value) noexcept { return value.type == type; },
            [&](const auto& carrier) noexcept {
                return std::ranges::contains(
                    context.plan().failure_abi().members(carrier.layout),
                    type
                );
            },
        }
    );
}

auto BodyRealizer::failure_projection(const FailureSource& source, TypeID type) noexcept
    -> TargetExpr {
    return source.visit(
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
                auto payload =
                    address_expression(dereference_expression(name_expression(slot.storage)));
                if (context.plan().failure_abi().members(slot.layout).size() == 1uz) {
                    return payload;
                }
                return template_call_expression(
                    intrinsic_expression(TargetSymbol::StdGetIf),
                    {context.lower_type(type)},
                    target_expressions(std::move(payload))
                );
            },
            [](const ValueFailureSource& value) static noexcept {
                return address_expression(name_expression(value.storage));
            },
        }
    );
}

auto BodyRealizer::dispatch_failure(
    const FailureSource& source,
    FailureSetID failures,
    const std::optional<FailureRelay>& relay
) noexcept -> LoweringStmtBuilder {
    auto transfers = LoweringStmtBuilder();
    auto candidates = std::vector<TypeID>();
    for (const auto type : context.plan().failure_abi().members(failures)) {
        if (failure_contains(source, type)) {
            candidates.push_back(type);
        }
    }
    if (!candidates.empty()) {
        if (const auto* value = std::get_if<ValueFailureSource>(&source)) {
            mutable_owners.insert(value->storage);
        }
    }
    for (const auto type : candidates) {
        const auto projection = fresh_local(TargetTemporaryNameKind::FailureProjection);
        transfers.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = projection,
                .type = context.pointer_type(context.intrinsic_type(TargetSymbol::Auto)),
                .initializer = failure_projection(source, type)
            },
            false
        );
        auto transfer = LoweringStmtBuilder();
        deliver_failure(
            transfer_expression(dereference_expression(name_expression(projection))),
            relay,
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
    if (tail_loop && std::holds_alternative<LoweringReturnResult>(result)) {
        const auto* awaited = std::get_if<SemAwait>(&preparation.operation(source).value);
        if (awaited != nullptr
            && std::ranges::find(tail_loop->selection.awaits, awaited)
                != tail_loop->selection.awaits.end()) {
            (co_await emit_tail_await(*awaited, destination));
            co_return {};
        }
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
    if (is_async()
        && !local_result_exit
        && std::holds_alternative<LoweringReturnResult>(result)
        && std::holds_alternative<SemAwait>(preparation.operation(source).value)) {
        auto completion = destination.accept((co_await expression(
            source,
            ConstantLiteralContext::Exact,
            ResultDemand::AdoptSuccess,
            delivered_region
        )));
        if (completion) {
            emit_completion(
                require_expression(std::move(*completion)),
                LoweringExitKind::FunctionReturn,
                destination
            );
        }
        co_return {};
    }
    const auto& expression_source = preparation.operation(source);
    const auto* call = std::get_if<SemCall>(&expression_source.value);
    const auto transport = fallible(expression_source);
    const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
    if (!is_async()
        && std::holds_alternative<LoweringReturnResult>(result)
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
                    LoweringExitSummary {
                        .entries = {
                            {.target = {LoweringExitKind::Failure, 0}, .needs_cleanup = false}
                        }
                    }
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
    const auto discarded_local_return = local_result_exit
        && std::holds_alternative<LoweringReturnResult>(result)
        && std::holds_alternative<LoweringDiscardResult>(local_result_exit->result);
    auto demand = discarded_local_return ? ResultDemand::Discard : ResultDemand::Value;
    if (!discarded_local_return
        && !is_async()
        && std::holds_alternative<LoweringReturnResult>(result)
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
                const auto closing_scopes = async_closing_locals(
                    std::same_as<Transfer, SemBreak> ? current_loop->break_async_scopes
                                                     : current_loop->continue_async_scopes
                );
                auto& loop = *current_loop;
                if constexpr (std::same_as<Transfer, SemBreak>) {
                    if (loop.expanded && !loop.break_label) {
                        loop.break_label = names.fresh(TargetTemporaryNameKind::Break);
                    }
                    if (loop.break_label) {
                        emit_async_exit(
                            generated_statement(
                                TargetGotoStmt {.label = *loop.break_label, .role = loop.jump_role}
                            ),
                            loop.break_target,
                            closing_scopes,
                            false,
                            destination
                        );
                    } else {
                        emit_async_exit(
                            generated_statement(TargetBreakStmt {}),
                            loop.break_target,
                            closing_scopes,
                            false,
                            destination
                        );
                    }
                } else {
                    if (loop.expanded && !loop.step) {
                        loop.step = names.fresh(TargetTemporaryNameKind::Continue);
                    }
                    if (loop.step) {
                        emit_async_exit(
                            generated_statement(
                                TargetGotoStmt {.label = *loop.step, .role = loop.jump_role}
                            ),
                            loop.target,
                            closing_scopes,
                            false,
                            destination
                        );
                    } else {
                        emit_async_exit(
                            generated_statement(TargetContinueStmt {}),
                            loop.target,
                            closing_scopes,
                            false,
                            destination
                        );
                    }
                }
                co_return {};
            },
            [&](const SemRethrow&) noexcept -> ContinuationTask<std::monostate> {
                transfer_failure(caught->source, caught->failures, current_failure, destination);
                co_return {};
            },
            [&](const SemThrow& value) noexcept -> ContinuationTask<std::monostate> {
                auto failure = read_value((co_await expression(value.value)), destination);
                if (failure) {
                    emit_failure(std::move(*failure), value.value, current_failure, destination);
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
            [&](const SemAsyncLet& value) noexcept -> ContinuationTask<std::monostate> {
                auto evaluation = LoweringStmtBuilder();
                auto operation = evaluation.accept((co_await operand({
                    .expression = std::addressof(value.initializer),
                    .use = PreparedUse::NativeTake,
                    .demand = PreparedDemand::Value,
                })));
                if (operation) {
                    const auto scope = std::ranges::find(
                        async_scopes,
                        metadata.binding(value.child).lifetime,
                        &AsyncScopeStorage::lifetime
                    );
                    if (scope == async_scopes.end()) {
                        invariant_violation("child owning region has no native scope");
                    }
                    auto start = call_member(
                        name_expression(scope->scope),
                        "start",
                        target_expressions(call_expression(
                            intrinsic_expression(TargetSymbol::StdMove),
                            target_expressions(std::move(*operation))
                        ))
                    );
                    if (evaluation.needs_cleanup()) {
                        const auto* operation_type = std::get_if<OperationTypeValue>(
                            &context.semantic()
                                 .types()
                                 .type(metadata.binding(value.child).type)
                                 .value
                        );
                        if (operation_type == nullptr) {
                            invariant_violation("child initializer has no canonical operation");
                        }
                        const auto storage = LoweringDeferredStorage {
                            .local = binding_locals.at(value.child),
                            .value_type = context.async_type(
                                TargetSymbol::RuntimeAsyncChild,
                                operation_type->success,
                                operation_type->failures
                            ),
                        };
                        delayed_bindings.emplace(value.child, storage);
                        declare_deferred(storage, false, destination, true);
                        // The nonmoving child is initialized in its lexical
                        // destination; initializer owners die at this source
                        // full-expression boundary after start has captured them.
                        initialize_deferred(storage, std::move(start), evaluation);
                        destination.scope(std::move(evaluation));
                    } else {
                        destination.append(std::move(evaluation));
                        destination.declare(
                            TargetVariableStmt {
                                .binding = TargetVariableBinding::MutableValue,
                                .maybe_unused = false,
                                .local = binding_locals.at(value.child),
                                .type = context.intrinsic_type(TargetSymbol::Auto),
                                .initializer = std::move(start),
                            },
                            true
                        );
                    }
                } else {
                    destination.scope(std::move(evaluation));
                }
                co_return {};
            },
            [&](const SemInitialize& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await initialize_binding(value, destination));
                co_return {};
            },
            [](const SemStaticBinding&) static noexcept -> ContinuationTask<std::monostate> {
                invariant_violation("static binding reached body realization");
            },
            [](const SemConstBlock&) static noexcept -> ContinuationTask<std::monostate> {
                invariant_violation("const block reached body realization");
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
            [&](const SemExpandedLoop& value) noexcept -> ContinuationTask<std::monostate> {
                (co_await lower_expanded_loop(value, destination));
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
