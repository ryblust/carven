module carven:backend.lowering.statements.mutation.impl;

import :backend.lowering.program;
import :backend.lowering.expressions;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.statements;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import std;

namespace {

constexpr auto lower_assignment_operator(HIRAssignmentOperator op) noexcept
    -> TargetAssignmentOperator {
    using enum HIRAssignmentOperator;
    switch (op) {
        case Assign:     return TargetAssignmentOperator::Assign;
        case Add:        return TargetAssignmentOperator::Add;
        case Subtract:   return TargetAssignmentOperator::Subtract;
        case Multiply:   return TargetAssignmentOperator::Multiply;
        case Divide:     return TargetAssignmentOperator::Divide;
        case Remainder:  return TargetAssignmentOperator::Remainder;
        case BitwiseAnd: return TargetAssignmentOperator::BitwiseAnd;
        case BitwiseOr:  return TargetAssignmentOperator::BitwiseOr;
        case BitwiseXor: return TargetAssignmentOperator::BitwiseXor;
        case LeftShift:  return TargetAssignmentOperator::LeftShift;
        case RightShift: return TargetAssignmentOperator::RightShift;
    }
    std::unreachable();
}

constexpr auto lower_update_operator(HIRUpdateOperator op) noexcept -> TargetUpdateOperator {
    switch (op) {
        case HIRUpdateOperator::Increment: return TargetUpdateOperator::Increment;
        case HIRUpdateOperator::Decrement: return TargetUpdateOperator::Decrement;
    }
    std::unreachable();
}

} // namespace

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRAssignmentStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto target = lower_mutation_expression(context, statement.target, control);
    auto value = lower_expression(context, statement.value, control);
    if (!TargetEvaluationSequencer::stable_place_source(context, statement.target)
        && (!value.prelude.empty()
            || !TargetEvaluationSequencer::effects_commute(
                context.semantic().expression_facts(statement.target).evaluation_effect,
                context.semantic().expression_facts(statement.value).evaluation_effect
            ))) {
        target = TargetEvaluationSequencer::materialize(
            context,
            std::move(target),
            MaterializationKind::WriteReference,
            TargetMaterializationReason::EvaluationOrder
        );
    }
    const auto target_value = target.expression;
    auto statements = std::move(target.prelude);
    statements.insert(
        statements.end(),
        std::make_move_iterator(value.prelude.begin()),
        std::make_move_iterator(value.prelude.end())
    );
    if (statement.op != HIRAssignmentOperator::Assign
        && statement.op != HIRAssignmentOperator::BitwiseAnd
        && statement.op != HIRAssignmentOperator::BitwiseOr
        && statement.op != HIRAssignmentOperator::BitwiseXor
        && is_integer_type(context, context.semantic().expression(statement.target).type)) {
        auto intrinsic = TargetSymbol::RuntimeIntegerAddAssign;
        switch (statement.op) {
            case HIRAssignmentOperator::Add:
                intrinsic = TargetSymbol::RuntimeIntegerAddAssign;
                break;
            case HIRAssignmentOperator::Subtract:
                intrinsic = TargetSymbol::RuntimeIntegerSubtractAssign;
                break;
            case HIRAssignmentOperator::Multiply:
                intrinsic = TargetSymbol::RuntimeIntegerMultiplyAssign;
                break;
            case HIRAssignmentOperator::Divide:
                intrinsic = TargetSymbol::RuntimeIntegerDivideAssign;
                break;
            case HIRAssignmentOperator::Remainder:
                intrinsic = TargetSymbol::RuntimeIntegerRemainderAssign;
                break;
            case HIRAssignmentOperator::LeftShift:
                intrinsic = TargetSymbol::RuntimeIntegerLeftShiftAssign;
                break;
            case HIRAssignmentOperator::RightShift:
                intrinsic = TargetSymbol::RuntimeIntegerRightShiftAssign;
                break;
            default: std::unreachable();
        }
        statements.push_back(context.target().append_lowering_statement(
            TargetExprStmt {
                .expression = context.target().append_expression({
                    .value = TargetCallExpr {
                        .callee = name_expression(context, intrinsic),
                        .template_arguments = {lower_type(
                            context,
                            context.semantic().expression(statement.target).type
                        )},
                        .arguments = {target_value, value.expression}
                    },
                }),
            }
        ));
        return TargetBlockStmt {.statements = std::move(statements), .scoped = false};
    }
    statements.push_back(context.target().append_lowering_statement(
        TargetAssignmentStmt {
            .target = target_value,
            .op = lower_assignment_operator(statement.op),
            .value = value.expression
        }
    ));
    return TargetBlockStmt {.statements = std::move(statements), .scoped = false};
}

auto lower_statement(
    TargetCallableLowerer& context,
    const HIRUpdateStmt& statement,
    const TargetControlDestinations& control
) noexcept -> TargetStmtValue {
    auto target = lower_mutation_expression(context, statement.target, control);
    const auto target_value = target.expression;
    auto statements = std::move(target.prelude);
    if (is_integer_type(context, context.semantic().expression(statement.target).type)) {
        statements.push_back(context.target().append_lowering_statement(
            TargetExprStmt {
                .expression = context.target().append_expression({
                    .value = TargetCallExpr {
                        .callee = name_expression(
                            context,
                            statement.op == HIRUpdateOperator::Increment
                                ? TargetSymbol::RuntimeIntegerIncrement
                                : TargetSymbol::RuntimeIntegerDecrement
                        ),
                        .template_arguments = {lower_type(
                            context,
                            context.semantic().expression(statement.target).type
                        )},
                        .arguments = {target_value},
                    },
                }),
            }
        ));
        return TargetBlockStmt {.statements = std::move(statements), .scoped = false};
    }
    statements.push_back(context.target().append_lowering_statement(
        TargetUpdateStmt {.op = lower_update_operator(statement.op), .target = target_value}
    ));
    return TargetBlockStmt {.statements = std::move(statements), .scoped = false};
}
