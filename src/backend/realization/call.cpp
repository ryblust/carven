module carven:backend.realization.call.impl;

import :backend.generation.plan;
import :backend.lowering.constant;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.expr;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.target.builder;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :semantic.semir.format;
import :semantic.semir.ids;
import :semantic.semir.program;
import :support.invariant;
import :support.visit;
import std;

auto BodyRealizer::ExpressionBuilder::complete_call(
    Fragment& fragment,
    const FallibleCall& transport,
    ResultDemand demand,
    PreparedUse use
) noexcept -> void {
    if (demand == ResultDemand::ForwardResult) {
        return;
    }
    const auto& call = transport.call;
    const auto outcome = owner.fresh_local(TargetTemporaryNameKind::Outcome);
    const auto storage =
        LoweringDeferredStorage {.local = outcome, .value_type = owner.context.call_result(call)};
    const auto needs_cleanup = owner.needs_cleanup(source(fragment).operation.type.resolved())
        || std::ranges::any_of(owner.context.plan().failure_abi().members(transport.failures),
                               [&](TypeID type) noexcept { return owner.needs_cleanup(type); });
    const auto automatic = fragment.local_storage;
    if (automatic) {
        statements.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = outcome,
                .type = storage.value_type,
                .initializer = raw(fragment, ConstantLiteralContext::Exact, PreparedUse::ReadBorrow)
            },
            needs_cleanup
        );
    } else {
        owner.declare_deferred(storage, false, reservations, needs_cleanup);
        owner.initialize_deferred(
            storage,
            raw(fragment, ConstantLiteralContext::Exact, PreparedUse::ReadBorrow),
            statements
        );
    }
    fragment.result_carrier = ResultCarrier {.local = outcome, .deferred = !automatic};
    const auto project_success = demand != ResultDemand::Discard
        && demand != ResultDemand::AdoptSuccess
        && !owner.context.is_void(source(fragment).operation.type.resolved());
    auto access = name_expression(outcome);
    if (!automatic) {
        access = dereference_expression(std::move(access));
    }
    if (owner.context.semantic().may_stop(call)) {
        auto stopped_access = name_expression(outcome);
        if (!automatic) {
            stopped_access = dereference_expression(std::move(stopped_access));
        }
        auto stopped = call_member(std::move(stopped_access), "is_stopped", {});
        auto exit = LoweringStmtBuilder();
        owner.emit_stopped(exit);
        statements.record_exits(exit.exits());
        statements.emit(
            generated_statement(
                TargetIfStmt {
                    .condition = std::move(stopped),
                    .body = std::move(exit).finish(),
                    .else_body = std::nullopt
                }
            ),
            true
        );
    }
    auto success = call_member(std::move(access), "success_if", {});
    if (project_success) {
        const auto constant = value_binding(use) == TargetVariableBinding::ConstValue
            || (use == PreparedUse::Consume && scalar(source(fragment).operation.type.resolved()));
        if (!constant) {
            owner.mutable_owners.insert(outcome);
        }
        const auto name = owner.fresh_local(TargetTemporaryNameKind::SuccessProjection);
        statements.declare(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = name,
                .type = owner.context.pointer_type(
                    owner.context.intrinsic_type(TargetSymbol::Auto, constant),
                    false
                ),
                .initializer = std::move(success)
            },
            false
        );
        success = name_expression(name);
        complete(fragment, Saved {.local = name, .kind = SavedKind::Success});
    } else {
        complete(fragment, LoweringCompleted {});
    }
    if (!owner.context.plan().failure_abi().members(transport.failures).empty()) {
        auto failure = LoweringStmtBuilder();
        owner.transfer_failure(
            OutcomeFailureSource {
                .storage = outcome,
                .deferred = !automatic,
                .layout = transport.failures
            },
            transport.failures,
            transport.destination,
            failure
        );
        statements.record_exits(failure.exits());
        statements.emit(
            generated_statement(
                TargetIfStmt {
                    .condition =
                        prefix_expression(TargetPrefixOperator::LogicalNot, std::move(success)),
                    .body = std::move(failure).finish(),
                    .else_body = std::nullopt
                }
            ),
            true
        );
    }
}
