module carven:backend.lowering.expr.operators.impl;

import :backend.lowering.program;
import :backend.lowering.expr;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.lowering.types;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.hir;
import :semantic.hir.expr;
import :semantic.hir.type;
import std;

namespace {

constexpr auto lower_binary_operator(HIRBinaryExpr::Operator op) noexcept -> TargetBinaryOperator {
    using enum HIRBinaryExpr::Operator;
    switch (op) {
        case LogicalOr:    return TargetBinaryOperator::LogicalOr;
        case LogicalAnd:   return TargetBinaryOperator::LogicalAnd;
        case BitwiseOr:    return TargetBinaryOperator::BitwiseOr;
        case BitwiseXor:   return TargetBinaryOperator::BitwiseXor;
        case BitwiseAnd:   return TargetBinaryOperator::BitwiseAnd;
        case Equal:        return TargetBinaryOperator::Equal;
        case NotEqual:     return TargetBinaryOperator::NotEqual;
        case Less:         return TargetBinaryOperator::Less;
        case LessEqual:    return TargetBinaryOperator::LessEqual;
        case Greater:      return TargetBinaryOperator::Greater;
        case GreaterEqual: return TargetBinaryOperator::GreaterEqual;
        case LeftShift:    return TargetBinaryOperator::LeftShift;
        case RightShift:   return TargetBinaryOperator::RightShift;
        case Add:          return TargetBinaryOperator::Add;
        case Subtract:     return TargetBinaryOperator::Subtract;
        case Multiply:     return TargetBinaryOperator::Multiply;
        case Divide:       return TargetBinaryOperator::Divide;
        case Remainder:    return TargetBinaryOperator::Remainder;
    }
    std::unreachable();
}

constexpr auto integer_intrinsic(HIRBinaryExpr::Operator op) noexcept
    -> std::optional<TargetSymbol> {
    using enum HIRBinaryExpr::Operator;
    switch (op) {
        case LeftShift:  return TargetSymbol::RuntimeIntegerLeftShift;
        case RightShift: return TargetSymbol::RuntimeIntegerRightShift;
        case Add:        return TargetSymbol::RuntimeIntegerAdd;
        case Subtract:   return TargetSymbol::RuntimeIntegerSubtract;
        case Multiply:   return TargetSymbol::RuntimeIntegerMultiply;
        case Divide:     return TargetSymbol::RuntimeIntegerDivide;
        case Remainder:  return TargetSymbol::RuntimeIntegerRemainder;
        default:         return std::nullopt;
    }
}

constexpr auto is_comparison(HIRBinaryExpr::Operator op) noexcept -> bool {
    using enum HIRBinaryExpr::Operator;
    return op == Equal
        || op == NotEqual
        || op == Less
        || op == LessEqual
        || op == Greater
        || op == GreaterEqual;
}

} // namespace

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID,
    const HIRTakeExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    auto operand = lower_expression(context, expression.operand_id, control);
    auto owner = expression.operand_id;
    while (true) {
        const auto* cast = std::get_if<HIRCastExpr>(&context.source().expression(owner).value);
        if (cast == nullptr || cast->kind != HIRCastKind::Identity) {
            break;
        }
        owner = cast->operand_id;
    }
    if (!std::holds_alternative<HIRNameExpr>(context.source().expression(owner).value)) {
        return operand;
    }
    return {
        .prelude = std::move(operand.prelude),
        .expression = call_expression(
            context,
            name_expression(context, TargetSymbol::StdMove),
            {operand.expression}
        ),
    };
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRUnaryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    auto operand = lower_expression(context, expression.operand_id, control);
    const auto operand_foreign =
        is_foreign_type(context, context.source().expression(expression.operand_id).type);
    if (expression.op == HIRUnaryExpr::Operator::LogicalNot && operand_foreign) {
        operand.expression = cpp_bool_cast(context, operand.expression);
    }
    if (expression.op == HIRUnaryExpr::Operator::Negate
        && is_integer_type(context, context.source().expression(expression.operand_id).type)) {
        return {
            .prelude = std::move(operand.prelude),
            .expression = context.target().append_expression({
                .value = TargetCallExpr {
                    .callee = name_expression(context, TargetSymbol::RuntimeIntegerNegate),
                    .template_argument_type_ids = {lower_type(context, source.type)},
                    .arguments = {operand.expression}
                },
            })
        };
    }
    auto op = TargetPrefixOperator::Negate;
    if (expression.op == HIRUnaryExpr::Operator::LogicalNot) {
        op = TargetPrefixOperator::LogicalNot;
    }
    if (expression.op == HIRUnaryExpr::Operator::BitwiseNot) {
        op = TargetPrefixOperator::BitwiseNot;
    }
    return {
        .prelude = std::move(operand.prelude),
        .expression = context.target().append_expression({
            .value = TargetPrefixExpr {.op = op, .operand_id = operand.expression},
        })
    };
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRBinaryExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    const auto& source = context.source().expression(id);
    if (expression.op == HIRBinaryExpr::Operator::LogicalOr
        || expression.op == HIRBinaryExpr::Operator::LogicalAnd) {
        const auto result_name = context.fresh_name(TargetTemporaryNameKind::Logic);
        auto left = lower_expression(context, expression.left, control);
        if (is_foreign_type(context, context.source().expression(expression.left).type)) {
            left.expression = cpp_bool_cast(context, left.expression);
        }
        auto prelude = std::move(left.prelude);
        prelude.push_back(context.target().append_lowering_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .name = result_name,
                .type = intrinsic_type(context, TargetSymbol::Bool),
                .initializer = left.expression,
                .maybe_unused = false
            }
        ));
        auto right = lower_expression(context, expression.right, control);
        if (is_foreign_type(context, context.source().expression(expression.right).type)) {
            right.expression = cpp_bool_cast(context, right.expression);
        }
        right.prelude.push_back(context.target().append_lowering_statement(
            TargetAssignmentStmt {
                .target = name_expression(context, TargetName {result_name}),
                .op = TargetAssignmentOperator::Assign,
                .value = right.expression
            }
        ));
        auto evaluate_right = name_expression(context, TargetName {result_name});
        if (expression.op == HIRBinaryExpr::Operator::LogicalOr) {
            evaluate_right = context.target().append_expression({
                .value = TargetPrefixExpr {
                    .op = TargetPrefixOperator::LogicalNot,
                    .operand_id = evaluate_right,
                },
            });
        }
        prelude.push_back(context.target().append_lowering_statement(
            TargetIfStmt {
                .branches = {TargetIfBranch {
                    .condition = evaluate_right,
                    .body = std::move(right.prelude)
                }},
                .else_body = std::nullopt
            }
        ));
        return {
            .prelude = std::move(prelude),
            .expression = name_expression(context, TargetName {result_name}),
        };
    }
    auto lowered_left = lower_expression(context, expression.left, control);
    auto lowered_right = lower_expression(context, expression.right, control);
    if (source.constant.has_value()) {
        auto prelude = std::move(lowered_left.prelude);
        prelude.insert(
            prelude.end(),
            std::make_move_iterator(lowered_right.prelude.begin()),
            std::make_move_iterator(lowered_right.prelude.end())
        );
        if (is_integer_type(context, context.source().expression(expression.left).type)) {
            const auto intrinsic = integer_intrinsic(expression.op);
            if (intrinsic.has_value()) {
                return {
                    .prelude = std::move(prelude),
                    .expression = context.target().append_expression({
                        .value = TargetCallExpr {
                            .callee = name_expression(context, *intrinsic),
                            .template_argument_type_ids = {lower_type(context, source.type)},
                            .arguments = {lowered_left.expression, lowered_right.expression}
                        },
                    })
                };
            }
        }
        return {
            .prelude = std::move(prelude),
            .expression = context.target().append_expression({
                .value = TargetBinaryExpr {
                    .left = lowered_left.expression,
                    .op = lower_binary_operator(expression.op),
                    .right = lowered_right.expression
                },
            })
        };
    }
    const auto left_foreign =
        is_foreign_type(context, context.source().expression(expression.left).type);
    const auto right_foreign =
        is_foreign_type(context, context.source().expression(expression.right).type);
    if (lowered_left.prelude.empty()
        && lowered_right.prelude.empty()
        && TargetEvaluationSequencer::expressions_commute(
            context,
            expression.left,
            expression.right
        )) {
        auto result = std::optional<TargetExprID>();
        if (is_integer_type(context, context.source().expression(expression.left).type)) {
            const auto intrinsic = integer_intrinsic(expression.op);
            if (intrinsic.has_value()) {
                result = context.target().append_expression({
                    .value = TargetCallExpr {
                        .callee = name_expression(context, *intrinsic),
                        .template_argument_type_ids = {lower_type(context, source.type)},
                        .arguments = {lowered_left.expression, lowered_right.expression},
                    },
                });
            }
        }
        if (!result.has_value()) {
            result = context.target().append_expression({
                .value = TargetBinaryExpr {
                    .left = lowered_left.expression,
                    .op = lower_binary_operator(expression.op),
                    .right = lowered_right.expression,
                },
            });
        }
        if ((left_foreign || right_foreign) && is_comparison(expression.op)) {
            result = cpp_bool_cast(context, *result);
        }
        return {.prelude = {}, .expression = *result};
    }
    auto left = TargetEvaluationSequencer::materialize(
        context,
        std::move(lowered_left),
        left_foreign ? MaterializationKind::Preserve
                     : TargetEvaluationSequencer::read_materialization(
                           context,
                           context.source().expression(expression.left).type
                       ),
        TargetMaterializationReason::EvaluationOrder
    );
    auto prelude = std::move(left.prelude);
    prelude.insert(
        prelude.end(),
        std::make_move_iterator(lowered_right.prelude.begin()),
        std::make_move_iterator(lowered_right.prelude.end())
    );
    auto result = std::optional<TargetExprID>();
    if (is_integer_type(context, context.source().expression(expression.left).type)) {
        const auto intrinsic = integer_intrinsic(expression.op);
        if (intrinsic.has_value()) {
            result = context.target().append_expression({
                .value = TargetCallExpr {
                    .callee = name_expression(context, *intrinsic),
                    .template_argument_type_ids = {lower_type(context, source.type)},
                    .arguments = {left.expression, lowered_right.expression}
                },
            });
        }
    }
    if (!result.has_value()) {
        result = context.target().append_expression({
            .value = TargetBinaryExpr {
                .left = left.expression,
                .op = lower_binary_operator(expression.op),
                .right = lowered_right.expression
            },
        });
    }
    if ((left_foreign || right_foreign) && is_comparison(expression.op)) {
        result = cpp_bool_cast(context, *result);
    }
    return {.prelude = std::move(prelude), .expression = *result};
}

auto lower_expression(
    TargetCallableLowerer& context,
    HIRExprID id,
    const HIRCastExpr& expression,
    const TargetControlDestinations& control
) noexcept -> LoweredExpression {
    auto operand = lower_expression(context, expression.operand_id, control);
    operand.expression = context.target().append_expression({
        .value = TargetStaticCastExpr {
            .type = lower_type(context, context.source().expression(id).type),
            .operand_id = operand.expression,
        },
    });
    return operand;
}
