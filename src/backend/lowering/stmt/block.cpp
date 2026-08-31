module carven:backend.lowering.stmt.block.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.lowering.stmt;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.hir;
import :semantic.hir.ids;
import :semantic.hir.stmt;
import :semantic.hir.type;
import std;

auto lower_block(
    TargetCallableLowerer& context,
    HIRBlockID id,
    const TargetControlDestinations& control,
    bool return_result
) noexcept -> std::vector<TargetStmtID> {
    const auto& block = context.source().block(id);
    const auto lexical_scope = context.enter_scope(block.scope);
    auto statements = lower_statements(context, block.statements, control);
    statements.reserve(statements.size() + static_cast<std::size_t>(block.result.has_value()));
    if (block.result.has_value()) {
        auto expression = lower_expression(context, *block.result, control);
        statements.insert(
            statements.end(),
            std::make_move_iterator(expression.prelude.begin()),
            std::make_move_iterator(expression.prelude.end())
        );
        auto result_statement = TargetStmtValue {
            TargetDiscardStmt {.expression = expression.expression},
        };
        if (return_result) {
            const auto returned =
                control.test_exit.has_value() && control.test_exit->result.has_value()
                ? test_success_expression(
                      context,
                      *control.test_exit->result,
                      expression.expression
                  )
                : expression.expression;
            result_statement = TargetReturnStmt {.expression = returned};
        }
        statements.push_back(
            context.target().append_lowering_statement(std::move(result_statement))
        );
    }
    return statements;
}

auto lower_statements(
    TargetCallableLowerer& context,
    std::span<const HIRStmtID> source,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    auto statements = std::vector<TargetStmtID>();
    statements.reserve(source.size());
    for (const auto statement : source) {
        statements.push_back(lower_statement(context, statement, control));
    }
    return statements;
}

auto lower_outcome_block(
    TargetCallableLowerer& context,
    HIRBlockID id,
    HIRTypeID result,
    FailureSetID failure_set,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID> {
    const auto natural_carrier = make_failure_carrier(context, result, failure_set);
    const auto matches_active_carrier =
        control.failure.has_value() && control.failure->carrier.shape == natural_carrier.shape;
    const auto carrier = matches_active_carrier ? control.failure->carrier : natural_carrier;
    const auto outcome_control = control.with_failure(
        FailureContinuation {
            .carrier = carrier,
            .local_transfer = std::nullopt,
        }
    );
    const auto& block = context.source().block(id);
    const auto lexical_scope = context.enter_scope(block.scope);
    auto statements = lower_statements(context, block.statements, outcome_control);
    if (block.result.has_value()) {
        auto value = lower_tail_expression(context, *block.result, outcome_control);
        statements.insert(
            statements.end(),
            std::make_move_iterator(value.prelude.begin()),
            std::make_move_iterator(value.prelude.end())
        );
        auto success = value.unconsumed_carrier.has_value()
            ? convert_tail_carrier(context, std::move(value), carrier).expression
            : outcome_success_expression(context, carrier.type, value.expression);
        if (control.test_exit.has_value() && control.test_exit->result.has_value()) {
            success = test_success_expression(context, *control.test_exit->result, success);
        }
        statements.push_back(context.target().append_lowering_statement(
            TargetReturnStmt {
                .expression = success,
            }
        ));
    } else if (is_void_type(context, result)) {
        auto success = outcome_success_expression(context, carrier.type);
        if (control.test_exit.has_value() && control.test_exit->result.has_value()) {
            success = test_success_expression(context, *control.test_exit->result, success);
        }
        statements.push_back(context.target().append_lowering_statement(
            TargetReturnStmt {
                .expression = success,
            }
        ));
    }
    return statements;
}
