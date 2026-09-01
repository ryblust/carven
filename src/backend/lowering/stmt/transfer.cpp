module carven:backend.lowering.stmt.transfer.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import std;

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRReturnStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto& callable_failure = control.callable_failure;
    if (!statement.value.has_value()) {
        return TargetReturnStmt {
            .expression = callable_failure.has_value()
                ? std::optional<TargetExprID> {outcome_success_expression(
                      context,
                      callable_failure->carrier.type,
                      std::nullopt
                  )}
                : std::nullopt,
        };
    }
    auto value = lower_tail_expression(context, *statement.value, control);
    if (callable_failure.has_value()) {
        if (value.unconsumed_carrier.has_value()) {
            value = convert_tail_carrier(context, std::move(value), callable_failure->carrier);
            return with_prelude(
                context,
                std::move(value.prelude),
                TargetReturnStmt {.expression = value.expression}
            );
        }
        return with_prelude(
            context,
            std::move(value.prelude),
            TargetReturnStmt {
                .expression = outcome_success_expression(
                    context,
                    callable_failure->carrier.type,
                    value.expression
                ),
            }
        );
    }
    return with_prelude(
        context,
        std::move(value.prelude),
        TargetReturnStmt {
            .expression = value.expression,
        }
    );
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRThrowStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto& continuation = control.failure;
    auto value = lower_expression(context, statement.value, control);
    if (!continuation.has_value()) {
        return with_prelude(
            context,
            std::move(value.prelude),
            TargetReturnStmt {.expression = value.expression}
        );
    }
    const auto failure =
        outcome_failure_expression(context, continuation->carrier.type, value.expression);
    if (!continuation->local_transfer.has_value()) {
        return with_prelude(
            context,
            std::move(value.prelude),
            TargetReturnStmt {.expression = failure}
        );
    }
    const auto& local_transfer = *continuation->local_transfer;
    auto statements = std::move(value.prelude);
    statements.push_back(context.target().append_lowering_statement(
        TargetAssignmentStmt {
            .target = name_expression(context, TargetName {local_transfer.destination_name}),
            .op = TargetAssignmentOperator::Assign,
            .value = failure,
        }
    ));
    statements.push_back(context.target().append_statement({
        .value =
            TargetGotoStmt {
                .label = local_transfer.transfer_label,
                .role = TargetJumpRole::FailureTransfer,
            },
        .attribution = {
            .kind = TargetAttributionKind::SourceExpansion,
            .origin = std::nullopt,
            .reason = TargetSyntheticReason::FailureTransport,
        },
    }));
    return TargetBlockStmt {.statements = std::move(statements), .scoped = true};
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRRethrowStmt&,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    const auto& continuation = control.failure;
    const auto& caught = control.caught_failure;
    if (!continuation.has_value() || !caught.has_value()) {
        return TargetReturnStmt {.expression = std::nullopt};
    }
    const auto failure = [&]() noexcept {
        if (caught->selected_failure_type.has_value()) {
            return outcome_failure_expression(
                context,
                continuation->carrier.type,
                take_outcome_failure(
                    context,
                    name_expression(context, TargetName {caught->carrier_name}),
                    *caught->selected_failure_type
                )
            );
        }
        const auto conversion =
            classify_carrier_conversion(context, caught->carrier, continuation->carrier);
        if (conversion == TargetCarrierConversion::Identity
            && !continuation->local_transfer.has_value()) {
            return name_expression(context, TargetName {caught->carrier_name});
        }
        return convert_carrier_expression(
            context,
            name_expression(context, TargetName {caught->carrier_name}),
            caught->carrier,
            continuation->carrier,
            CarrierInputCategory::Lvalue
        );
    }();
    if (!continuation->local_transfer.has_value()) {
        return TargetReturnStmt {.expression = failure};
    }
    const auto& local_transfer = *continuation->local_transfer;
    return TargetBlockStmt {
        .statements =
            {
                context.target().append_lowering_statement(
                    TargetAssignmentStmt {
                        .target =
                            name_expression(context, TargetName {local_transfer.destination_name}),
                        .op = TargetAssignmentOperator::Assign,
                        .value = failure,
                    }
                ),
                context.target().append_statement({
                    .value =
                        TargetGotoStmt {
                            .label = local_transfer.transfer_label,
                            .role = TargetJumpRole::FailureTransfer,
                        },
                    .attribution =
                        {
                            .kind = TargetAttributionKind::SourceExpansion,
                            .origin = std::nullopt,
                            .reason = TargetSyntheticReason::FailureTransport,
                        },
                }),
            },
        .scoped = true,
    };
}

auto lower_statement(
    TargetCallableLowerer&,
    const HIRBreakStmt&,
    const TargetControlDestinations&
) noexcept -> TargetStmtValue {
    return TargetBreakStmt {};
}

auto lower_statement(
    TargetCallableLowerer&,
    const HIRContinueStmt&,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    if (control.continue_destination.has_value()) {
        control.continue_destination->used.get() = true;
        return TargetGotoStmt {
            .label = control.continue_destination->label,
            .role = TargetJumpRole::ForLoopContinue,
        };
    }
    return TargetContinueStmt {};
}
