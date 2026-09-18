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
    bool project_success,
    PreparedUse use,
    bool direct,
    bool propagate_outcome
) noexcept -> void {
    const auto callee_type =
        std::get<SemCall>(source(fragment).operation.value).callee->type.resolved();
    const auto outcome = owner.fresh_local(TargetTemporaryNameKind::Outcome);
    const auto storage = LoweringDeferredStorage {
        .local = outcome,
        .value_type = owner.context.call_result(callee_type)
    };
    // All operand fragments have been completed before choosing storage. A
    // direct root has no retained auxiliary owners or shared execution;
    // its ordinary Outcome local therefore preserves reverse destruction.
    if (direct) {
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = outcome,
                .type = storage.value_type,
                .initializer = raw(fragment)
            }
        ));
    } else {
        owner.declare_deferred(storage, false, declarations);
        owner.initialize_deferred(storage, raw(fragment), statements);
    }
    auto access = name_expression(outcome);
    if (!direct) {
        access = dereference_expression(std::move(access));
    }
    if (propagate_outcome) {
        // Keep the original carrier and operand cleanup owners. Runtime uses
        // the same payload transfer policy as projection followed by return.
        complete(
            fragment,
            call_member(
                call_expression(
                    intrinsic_expression(TargetSymbol::StdMove),
                    target_expressions(std::move(access))
                ),
                "propagate",
                {}
            )
        );
        return;
    }
    if (owner.context.semantic().may_stop_test(callee_type)) {
        auto stopped_access = name_expression(outcome);
        if (!direct) {
            stopped_access = dereference_expression(std::move(stopped_access));
        }
        auto stopped = template_call_expression(
            member_expression(
                std::move(stopped_access),
                TargetIdentifier::from_spelling("failure_if")
            ),
            {owner.context.intrinsic_type(TargetSymbol::RuntimeTestStopped)},
            {}
        );
        auto exit = LoweringStmtBuilder();
        owner.emit_test_exit(exit);
        statements.record_exits(exit.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back({.condition = std::move(stopped), .body = std::move(exit).finish()});
        statements.emit(generated_statement(
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    }
    auto success = call_member(std::move(access), "success_if", {});
    if (project_success) {
        const auto name = owner.fresh_local(TargetTemporaryNameKind::SuccessProjection);
        statements.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::ConstValue,
                .maybe_unused = false,
                .local = name,
                .type = owner.context.pointer_type(owner.context.intrinsic_type(
                    TargetSymbol::Auto,
                    use == PreparedUse::ReadBorrow
                        || use == PreparedUse::ConstPlace
                        || use == PreparedUse::AddressValue
                        || use == PreparedUse::OperandValue
                        || (use == PreparedUse::Consume
                            && scalar(source(fragment).operation.type.resolved()))
                )),
                .initializer = std::move(success)
            }
        ));
        success = name_expression(name);
        complete(fragment, Saved {.local = name, .kind = SavedKind::Success});
    } else {
        complete(fragment, LoweringCompleted {});
    }
    auto failure = owner.dispatch_failure(
        OutcomeFailureSource {.storage = outcome, .deferred = !direct},
        transport.failures,
        transport.destination
    );
    statements.record_exits(failure.exits());
    auto branches = std::vector<TargetIfBranch>();
    branches.push_back(
        {.condition = prefix_expression(TargetPrefixOperator::LogicalNot, std::move(success)),
         .body = std::move(failure).finish()}
    );
    statements.emit(generated_statement(
        TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
    ));
}
