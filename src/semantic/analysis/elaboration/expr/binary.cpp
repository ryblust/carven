module carven:semantic.analysis.elaboration.expr.binary.impl;

import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.literal;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.operations;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.type;
import std;

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTBinaryExpr& binary,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    const auto contextual_numeric_literal = [&](ASTExprID id) noexcept {
        const auto* literal = std::get_if<ASTLiteral>(&ast.expression(id).value);
        const auto numeric = literal == nullptr ? std::nullopt : as_numeric_literal(*literal);
        return numeric.has_value() && numeric_suffix(*numeric) == NumericSuffix::None;
    };
    const auto contextual_case = [&](ASTExprID id) noexcept {
        const auto& candidate = ast.expression(id).value;
        if (std::holds_alternative<ASTContextualCaseExpr>(candidate)) {
            return true;
        }
        const auto* call = std::get_if<ASTCallExpr>(&candidate);
        return call != nullptr
            && std::holds_alternative<ASTContextualCaseExpr>(ast.expression(call->callee).value);
    };
    const auto equality =
        binary.op == ASTBinaryOperator::Equal || binary.op == ASTBinaryOperator::NotEqual;
    const auto [left, right] = [&]() noexcept -> std::pair<HIRExprID, HIRExprID> {
        if (equality && contextual_case(binary.left) && !contextual_case(binary.right)) {
            const auto right = build_expression(module_analysis, scopes, control, binary.right);
            return {
                build_expected_expression(
                    module_analysis,
                    scopes,
                    control,
                    binary.left,
                    expression_type(module_analysis, right)
                ),
                right,
            };
        }
        if (equality && contextual_case(binary.right) && !contextual_case(binary.left)) {
            const auto left = build_expression(module_analysis, scopes, control, binary.left);
            return {
                left,
                build_expected_expression(
                    module_analysis,
                    scopes,
                    control,
                    binary.right,
                    expression_type(module_analysis, left)
                ),
            };
        }
        if (contextual_numeric_literal(binary.left) && !contextual_numeric_literal(binary.right)) {
            const auto right = build_expression(module_analysis, scopes, control, binary.right);
            return {
                build_expected_expression(
                    module_analysis,
                    scopes,
                    control,
                    binary.left,
                    expression_type(module_analysis, right)
                ),
                right,
            };
        }
        const auto left = build_expression(module_analysis, scopes, control, binary.left);
        const auto right = contextual_numeric_literal(binary.right)
            ? build_expected_expression(
                  module_analysis,
                  scopes,
                  control,
                  binary.right,
                  expression_type(module_analysis, left)
              )
            : build_expression(module_analysis, scopes, control, binary.right);
        return {left, right};
    }();
    auto op = HIRBinaryExpr::Operator::Add;
    switch (binary.op) {
        case ASTBinaryOperator::LogicalOr:    op = HIRBinaryExpr::Operator::LogicalOr; break;
        case ASTBinaryOperator::LogicalAnd:   op = HIRBinaryExpr::Operator::LogicalAnd; break;
        case ASTBinaryOperator::BitwiseOr:    op = HIRBinaryExpr::Operator::BitwiseOr; break;
        case ASTBinaryOperator::BitwiseXor:   op = HIRBinaryExpr::Operator::BitwiseXor; break;
        case ASTBinaryOperator::BitwiseAnd:   op = HIRBinaryExpr::Operator::BitwiseAnd; break;
        case ASTBinaryOperator::Equal:        op = HIRBinaryExpr::Operator::Equal; break;
        case ASTBinaryOperator::NotEqual:     op = HIRBinaryExpr::Operator::NotEqual; break;
        case ASTBinaryOperator::Less:         op = HIRBinaryExpr::Operator::Less; break;
        case ASTBinaryOperator::LessEqual:    op = HIRBinaryExpr::Operator::LessEqual; break;
        case ASTBinaryOperator::Greater:      op = HIRBinaryExpr::Operator::Greater; break;
        case ASTBinaryOperator::GreaterEqual: op = HIRBinaryExpr::Operator::GreaterEqual; break;
        case ASTBinaryOperator::LeftShift:    op = HIRBinaryExpr::Operator::LeftShift; break;
        case ASTBinaryOperator::RightShift:   op = HIRBinaryExpr::Operator::RightShift; break;
        case ASTBinaryOperator::Add:          op = HIRBinaryExpr::Operator::Add; break;
        case ASTBinaryOperator::Subtract:     op = HIRBinaryExpr::Operator::Subtract; break;
        case ASTBinaryOperator::Multiply:     op = HIRBinaryExpr::Operator::Multiply; break;
        case ASTBinaryOperator::Divide:       op = HIRBinaryExpr::Operator::Divide; break;
        case ASTBinaryOperator::Remainder:    op = HIRBinaryExpr::Operator::Remainder; break;
    }
    const auto& builder = module_analysis.builder();
    const auto left_type = expression_type(module_analysis, left);
    const auto right_type = expression_type(module_analysis, right);
    const auto operands_compatible = compatible(module_analysis, left_type, right_type);
    const auto equality_capable = !equality
        || is_error_type(module_analysis, left_type)
        || supports_equality(module_analysis, left_type);
    const auto check = check_binary_operator(builder, op, left_type, right_type, equality_capable);
    if (!operands_compatible) {
        module_analysis.emit(
            binary.operator_span,
            "binary operands have incompatible types",
            DiagnosticCode::TypeBinary
        );
    }
    if (!check.equality_supported) {
        module_analysis.emit(
            binary.operator_span,
            "operand type does not support structural equality",
            DiagnosticCode::TypeEqualityUnsupported
        );
    }
    const auto ordered = op == HIRBinaryExpr::Operator::Less
        || op == HIRBinaryExpr::Operator::LessEqual
        || op == HIRBinaryExpr::Operator::Greater
        || op == HIRBinaryExpr::Operator::GreaterEqual;
    switch (check.status) {
        case BinaryOperatorStatus::BooleanOperandsRequired:
            module_analysis.emit(
                binary.operator_span,
                "logical operands must have type bool",
                DiagnosticCode::TypeLogicalBool
            );
            break;
        case BinaryOperatorStatus::NumericOperandsRequired:
            module_analysis.emit(
                binary.operator_span,
                ordered ? "ordered comparison requires numeric operands"
                        : "arithmetic operands must have numeric types",
                ordered ? DiagnosticCode::TypeBinaryOrdered : DiagnosticCode::TypeBinaryNumeric
            );
            break;
        case BinaryOperatorStatus::IntegerOperandsRequired:
            module_analysis.emit(
                binary.operator_span,
                "integer operator requires integer operands",
                DiagnosticCode::TypeBinaryInteger
            );
            break;
        case BinaryOperatorStatus::Supported:
        case BinaryOperatorStatus::Error:     break;
    }
    const auto result_builtin = operator_result_builtin(check.result);
    const auto result_type = result_builtin.has_value()
        ? builtin(module_analysis, binary.operator_span, *result_builtin)
        : left_type;
    auto constant = std::optional<HIRConstant>();
    auto evaluation = evaluate_binary_constant(
        builder,
        op,
        left,
        right,
        result_type,
        operands_compatible,
        equality_capable
    );
    if (evaluation.has_value()) {
        constant = std::move(evaluation->value);
    } else {
        switch (evaluation.error()) {
            case HIRConstantEvaluationFailure::IntegerOverflow:
                module_analysis.emit(
                    binary.operator_span,
                    "integer constant overflow",
                    DiagnosticCode::ConstOverflow
                );
                break;
            case HIRConstantEvaluationFailure::DivideByZero:
                module_analysis.emit(
                    binary.operator_span,
                    "division by zero in constant expression",
                    DiagnosticCode::ConstDivideByZero
                );
                break;
            case HIRConstantEvaluationFailure::ShiftOutOfRange:
                module_analysis.emit(
                    binary.operator_span,
                    "shift count is outside the integer type width",
                    DiagnosticCode::ConstShiftRange
                );
                break;
            case HIRConstantEvaluationFailure::OperandNotConstant:
            case HIRConstantEvaluationFailure::UnsupportedOperation:
            case HIRConstantEvaluationFailure::InvalidOperation:     break;
        }
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = result_type,
            .constant = std::move(constant),
            .value = HIRBinaryExpr {
                .left = left,
                .op = op,
                .right = right,
            },
        }
    );
}
