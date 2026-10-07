module carven:backend.realization.async.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.expr;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.async;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :support.invariant;
import std;

auto BodyRealizer::is_async() const noexcept -> bool {
    const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
    return callable != nullptr
        && context.semantic()
               .callable_signatures()
               .signature(
                   context.semantic().declarations().callable(callable->callable_id).signature
               )
               .execution
        == CallableExecutionKind::Async;
}

auto BodyRealizer::completion_type() noexcept -> TargetTypeID {
    const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit);
    if (callable == nullptr || !is_async()) {
        invariant_violation("async completion requested for an ordinary body");
    }
    return context.callable_completion(callable->callable_id);
}

auto BodyRealizer::begin_async_region(
    const SemanticRegion& source,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!is_async()
        || preparation.children(source.lifetime).empty()
        || std::ranges::any_of(async_scopes, [&](const AsyncScopeStorage& scope) noexcept {
               return scope.lifetime == source.lifetime;
           })) {
        return;
    }
    const auto scope = fresh_local(TargetTemporaryNameKind::Owner);
    async_scopes.push_back({.lifetime = source.lifetime, .scope = scope});
    const auto type = context.intrinsic_type(TargetSymbol::RuntimeAsyncChildScope);
    destination.declare(
        TargetVariableStmt {
            .binding = TargetVariableBinding::MutableValue,
            .maybe_unused = false,
            .local = scope,
            .type = type,
            .initializer =
                TargetExpr {
                    .value =
                        TargetConstructionExpr {
                            .type = type,
                            .initializer = target_expressions(co_await_expression(call_expression(
                                intrinsic_expression(TargetSymbol::RuntimeAsyncCurrentActivation),
                                {}
                            ))),
                        }
                },
        },
        true
    );
}

auto BodyRealizer::async_closing_locals(std::size_t retained) const noexcept
    -> std::vector<TargetLocalID> {
    if (retained > async_scopes.size()) {
        invariant_violation("native async close boundary is outside its lexical scopes");
    }
    auto result = std::vector<TargetLocalID>();
    for (auto index = async_scopes.size(); index > retained; --index) {
        result.push_back(async_scopes[index - 1uz].scope);
    }
    return result;
}

auto BodyRealizer::leave_async_scopes(std::size_t retained) noexcept -> void {
    if (retained > async_scopes.size()) {
        invariant_violation("native async scope leave crosses its lexical boundary");
    }
    while (async_scopes.size() > retained) {
        async_scopes.pop_back();
    }
}

auto BodyRealizer::close_async_locals(
    std::span<const TargetLocalID> scopes,
    bool cancel,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (!is_async() || !destination.continues()) {
        return;
    }
    for (const auto scope : scopes) {
        auto policy = static_member_expression(
            context.intrinsic_type(TargetSymbol::RuntimeAsyncClosingPolicy),
            TargetIdentifier::from_spelling(cancel ? "RequestCancelAndClose" : "CloseOnly")
        );
        destination.emit(statement_expression(co_await_expression(
            call_member(name_expression(scope), "close", target_expressions(std::move(policy)))
        )));
    }
}

auto BodyRealizer::close_async_scopes(
    std::size_t retained,
    bool cancel,
    LoweringStmtBuilder& destination
) noexcept -> void {
    close_async_locals(async_closing_locals(retained), cancel, destination);
}

auto BodyRealizer::emit_completion(
    TargetExpr completion,
    LoweringExitKind kind,
    LoweringStmtBuilder& destination
) noexcept -> void {
    if (async_scopes.empty()) {
        destination.terminate(
            generated_statement(TargetCoReturnStmt {.expression = std::move(completion)}),
            {kind, 0}
        );
        return;
    }
    // The selected completion outlives every lexical cleanup hop.
    if (!pending_completion) {
        pending_completion = LoweringDeferredStorage {
            .local = fresh_local(TargetTemporaryNameKind::Outcome),
            .value_type = completion_type(),
        };
    }
    initialize_deferred(*pending_completion, std::move(completion), destination);
    emit_async_exit(
        generated_statement(
            TargetCoReturnStmt {
                .expression = call_expression(
                    intrinsic_expression(TargetSymbol::StdMove),
                    target_expressions(
                        dereference_expression(name_expression(pending_completion->local))
                    )
                ),
            }
        ),
        {kind, 0},
        async_closing_locals(0uz),
        kind == LoweringExitKind::Failure || kind == LoweringExitKind::Cancelled,
        destination
    );
}

auto BodyRealizer::emit_cancelled(LoweringStmtBuilder& destination) noexcept -> void {
    emit_completion(
        call_expression(
            static_member_expression(
                completion_type(),
                TargetIdentifier::from_spelling("cancelled")
            ),
            {}
        ),
        LoweringExitKind::Cancelled,
        destination
    );
}

auto BodyRealizer::ExpressionBuilder::native_await_intrinsic(const SemAwait& source) noexcept
    -> const SemAsyncIntrinsic* {
    if (source.operand_kind != AsyncAwaitOperandKind::ColdOperation) {
        return nullptr;
    }
    const auto* intrinsic =
        std::get_if<SemAsyncIntrinsic>(&BodyPreparation::operation(*source.operand).value);
    if (intrinsic == nullptr
        || (intrinsic->kind != AsyncIntrinsic::YieldOnce
            && intrinsic->kind != AsyncIntrinsic::CancellationPoint)) {
        return nullptr;
    }
    return intrinsic;
}

auto BodyRealizer::ExpressionBuilder::complete_await(
    Fragment& fragment,
    TargetExpr invocation,
    const SemAwait& source,
    bool project_success,
    PreparedUse use
) noexcept -> void {
    complete_awaited(
        fragment,
        co_await_expression(std::move(invocation)),
        source,
        project_success,
        use
    );
}

auto BodyRealizer::ExpressionBuilder::complete_awaited(
    Fragment& fragment,
    TargetExpr awaited,
    const SemAwait& source,
    bool project_success,
    PreparedUse use
) noexcept -> void {
    const auto operation = source.operand->type.resolved();
    const auto* operation_type =
        std::get_if<OperationTypeValue>(&owner.context.semantic().types().type(operation).value);
    if (operation_type == nullptr) {
        invariant_violation("await has no canonical operation type");
    }
    const auto completion = owner.fresh_local(TargetTemporaryNameKind::Outcome);
    const auto type = owner.context.async_type(
        TargetSymbol::RuntimeAsyncCompletion,
        operation_type->success,
        operation_type->failures
    );
    const auto automatic = fragment.local_storage;
    const auto storage = LoweringDeferredStorage {.local = completion, .value_type = type};
    if (automatic) {
        statements.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = completion,
                .type = type,
                .initializer = std::move(awaited),
            },
            true
        );
    } else {
        owner.declare_deferred(storage, false, reservations, true);
        // The in-place factory receives an already completed owning carrier;
        // any suspension occurred in the enclosing coroutine before this point.
        const auto ready = owner.fresh_local(TargetTemporaryNameKind::Outcome);
        statements.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = ready,
                .type = type,
                .initializer = std::move(awaited),
            },
            true
        );
        owner.initialize_deferred(
            storage,
            call_expression(
                intrinsic_expression(TargetSymbol::StdMove),
                target_expressions(name_expression(ready))
            ),
            statements
        );
    }
    const auto access = [&]() noexcept {
        auto value = name_expression(completion);
        return automatic ? std::move(value) : dereference_expression(std::move(value));
    };
    if (owner.context.semantic()
            .await_completion_may_be_cancelled(owner.preparation.body().id(), source)) {
        auto cancelled = LoweringStmtBuilder();
        owner.emit_cancelled(cancelled);
        statements.record_exits(cancelled.exits());
        auto cancelled_branches = std::vector<TargetIfBranch>();
        cancelled_branches.push_back(
            {.condition = call_member(access(), "is_cancelled", {}),
             .body = std::move(cancelled).finish()}
        );
        statements.emit(generated_statement(
            TargetIfStmt {.branches = std::move(cancelled_branches), .else_body = std::nullopt}
        ));
    }
    auto success = call_member(access(), "success_if", {});
    if (!owner.context.plan().failure_abi().members(operation_type->failures).empty()) {
        auto failure = LoweringStmtBuilder();
        owner.transfer_failure(
            OutcomeFailureSource {
                .storage = completion,
                .deferred = !automatic,
                .layout = operation_type->failures,
            },
            operation_type->failures,
            owner.current_failure,
            failure
        );
        statements.record_exits(failure.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = prefix_expression(
                 TargetPrefixOperator::LogicalNot,
                 call_member(access(), "success_if", {})
             ),
             .body = std::move(failure).finish()}
        );
        statements.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    if (const auto* builtin = std::get_if<BuiltinTypeValue>(
            &owner.context.semantic().types().type(operation_type->success).value
        );
        builtin != nullptr && builtin->kind == BuiltinType::Char) {
        statements.emit(statement_expression(call_expression(
            intrinsic_expression(TargetSymbol::RuntimeCheckedUnicodeScalar),
            target_expressions(
                member_expression(
                    dereference_expression(call_member(access(), "success_if", {})),
                    TargetIdentifier::from_spelling("value")
                ),
                source_site_expression(owner.context, this->source(fragment).operation.origin)
            )
        )));
    }
    fragment.awaited_carrier =
        AwaitedCarrier {.local = completion, .deferred = !automatic, .operation = operation};
    if (project_success && !owner.context.is_void(operation_type->success)) {
        const auto projection = owner.fresh_local(TargetTemporaryNameKind::SuccessProjection);
        statements.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = projection,
                .type = owner.context.pointer_type(owner.context.intrinsic_type(
                    TargetSymbol::Auto,
                    use == PreparedUse::ReadBorrow
                        || use == PreparedUse::ConstPlace
                        || use == PreparedUse::AddressValue
                        || use == PreparedUse::OperandValue
                        || (use == PreparedUse::Consume && scalar(operation_type->success))
                )),
                .initializer = std::move(success),
            },
            false
        );
        complete(fragment, Saved {.local = projection, .kind = SavedKind::Success});
    } else {
        complete(fragment, LoweringCompleted {});
    }
}

auto BodyRealizer::select_await_producer(const SemanticExpression& source) noexcept
    -> std::optional<PreparedAwaitProducer> {
    auto prepared = prepare_await_producer(context.semantic(), source);
    const auto available = [&](CallableID callable) noexcept {
        return context.names().callable_owner(callable) == context.active_module()
            && std::ranges::find(fusion.active, callable) == fusion.active.end();
    };
    if (!prepared
        || !available(prepared->callable)
        || (prepared->factory && !available(prepared->factory->callable))
        || prepared->nodes > fusion.remaining) {
        return std::nullopt;
    }
    fusion.remaining -= prepared->nodes;
    return prepared;
}

auto BodyRealizer::ExpressionBuilder::snapshot_parameters(
    CallableID callable,
    BodyID body,
    std::vector<TargetExpr> operands
) noexcept -> BodyLoweringInputs {
    const auto& program = owner.context.semantic();
    const auto& metadata = program.bodies().body(body);
    const auto& signature = program.callable_signatures().signature(
        program.declarations().callable(callable).signature
    );
    // Snapshot scalar actual arguments before entering the selected body,
    // including formals unused or referenced repeatedly by a transparent factory.
    auto inputs = BodyLoweringInputs {
        .parameters = {},
        .captures = {},
        .exit = CallableBodyExit {.callable_id = callable}
    };
    for (auto index = 0uz; index < operands.size(); ++index) {
        const auto local = owner.fresh_local(TargetTemporaryNameKind::Operand);
        statements.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = true,
                .local = local,
                .type = owner.context.lower_type(signature.parameters[index].type),
                .initializer = std::move(operands[index]),
            },
            false
        );
        inputs.parameters.emplace_back(metadata.inputs().parameters[index], local);
    }
    return inputs;
}

auto BodyRealizer::ExpressionBuilder::fuse_call(
    const SemColdCall& call,
    BodyID body,
    std::vector<TargetExpr> operands,
    ResultDemand demand
) noexcept -> std::optional<TargetLocalID> {
    const auto& program = owner.context.semantic();
    auto inputs = snapshot_parameters(call.target, body, std::move(operands));
    const auto& signature = program.callable_signatures().signature(
        program.declarations().callable(call.target).signature
    );
    auto storage = std::optional<TargetLocalID>();
    auto result = LoweringResultDestination(LoweringDiscardResult {});
    if (demand != ResultDemand::Discard && !owner.context.is_void(signature.result)) {
        // Preparation proves a scalar result. Every normal exit assigns it
        // before the producer scope ends; no dynamic construction state is needed.
        storage = owner.fresh_local(TargetTemporaryNameKind::Owner);
        const auto type = owner.context.lower_type(signature.result);
        statements.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = true,
                .local = *storage,
                .type = type,
                .initializer =
                    TargetExpr {.value = TargetConstructionExpr {.type = type, .initializer = {}}},
            },
            false
        );
        result = LoweringAssignResult {.local = *storage};
    }
    const auto preparation = BodyPreparation(program, body);
    owner.fusion.active.push_back(call.target);
    auto realized =
        BodyRealizer(owner.context, preparation, std::move(inputs), &owner, result).finish();
    owner.fusion.active.pop_back();
    for (auto index = 0uz; index < realized.statements.size(); ++index) {
        statements.emit(
            std::move(realized.statements[index]),
            realized.continues || index + 1uz != realized.statements.size()
        );
    }
    return storage;
}

auto BodyRealizer::ExpressionBuilder::fuse_factory(
    const SemCall& call,
    const PreparedAwaitProducer& producer,
    std::vector<TargetExpr> operands,
    ResultDemand demand
) noexcept -> std::optional<TargetLocalID> {
    const auto& route = *producer.factory;
    auto inputs = snapshot_parameters(*call.target, route.body, std::move(operands));
    owner.fusion.active.push_back(*call.target);
    const auto preparation = BodyPreparation(owner.context.semantic(), route.body);
    auto factory = BodyRealizer(owner.context, preparation, std::move(inputs), &owner);
    auto arguments = std::vector<TargetExpr>();
    for (const auto& argument : route.returned->arguments) {
        const auto& expression = BodyPreparation::operation(argument.expression);
        if (const auto* binding = std::get_if<SemBinding>(&expression.value)) {
            arguments.push_back(factory.binding_expression(binding->binding));
        } else {
            arguments.push_back(constant_expression(
                owner.context,
                std::get<SemConstant>(expression.value).constant,
                ConstantLiteralContext::Exact
            ));
        }
    }
    const auto result = fuse_call(*route.returned, producer.body, std::move(arguments), demand);
    owner.fusion.active.pop_back();
    return result;
}

auto BodyRealizer::emit_tail_await(
    const SemAwait& source,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    const auto& call = std::get<SemColdCall>(preparation.operation(*source.operand).value);
    auto snapshots = std::vector<TargetLocalID>();
    for (auto index = 0uz; index < call.arguments.size(); ++index) {
        auto value = destination.accept((co_await operand(
            PreparedOperand {
                .expression = &call.arguments[index].expression,
                .use = PreparedUse::OperandValue,
                .demand = PreparedDemand::Value
            }
        )));
        if (!value) {
            co_return {};
        }
        const auto local = fresh_local(TargetTemporaryNameKind::Operand);
        destination.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = local,
                .type = context.lower_type(metadata.binding(inputs.parameters[index].first).type),
                .initializer = std::move(*value)
            },
            false
        );
        snapshots.push_back(local);
    }
    // Every actual observes this iteration, including swapped and repeated
    // parameters. No next-iteration slot changes until all actuals finish.
    for (auto index = 0uz; index < snapshots.size(); ++index) {
        destination.emit(generated_statement(
            TargetAssignmentStmt {
                .target = name_expression(tail_loop->slots[index]),
                .op = TargetAssignmentOperator::Assign,
                .value = name_expression(snapshots[index])
            }
        ));
    }
    // The same receiver works in source loop headers, steps, and bodies;
    // lowering order does not determine the eventual native loop nesting.
    destination.terminate(
        generated_statement(
            TargetGotoStmt {.label = tail_loop->next_label, .role = TargetJumpRole::ForLoopContinue}
        ),
        tail_loop->next
    );
    co_return {};
}

auto BodyRealizer::wrap_tail_loop(LoweringStmtBuilder statements) noexcept -> LoweringStmtBuilder {
    auto outer = LoweringStmtBuilder();
    auto body = LoweringStmtBuilder();
    for (auto index = 0uz; index < inputs.parameters.size(); ++index) {
        const auto& [binding, input] = inputs.parameters[index];
        const auto type = context.lower_type(metadata.binding(binding).type);
        outer.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = tail_loop->slots[index],
                .type = type,
                .initializer = name_expression(input)
            },
            false
        );
        body.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = true,
                .local = binding_locals.at(binding),
                .type = type,
                .initializer = name_expression(tail_loop->slots[index])
            },
            false
        );
    }
    body.append(std::move(statements));
    auto iteration = LoweringStmtBuilder();
    iteration.scope(std::move(body));
    if (iteration.exits().contains(tail_loop->next)) {
        iteration.resume(tail_loop->next_label, TargetJumpRole::ForLoopContinue, tail_loop->next);
    }
    outer.record_exits(iteration.exits());
    outer.emit(
        generated_statement(
            TargetWhileStmt {
                .condition = bool_expression(true),
                .body = std::move(iteration).finish()
            }
        ),
        false
    );
    return outer;
}
