module carven:semantic.analysis.elaboration.expr.operators.impl;

import :frontend.ast.control;
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
    const ASTPrefixExpr& prefix,
    ASTExprID,
    ProgramOriginID expression_origin,
    std::optional<HIRTypeID> expected
) noexcept -> HIRExprID {
    const auto ast = module_analysis.syntax();
    auto literal_id = prefix.operand_id;
    while (const auto* group = std::get_if<ASTGroupExpr>(&ast.expression(literal_id).value)) {
        literal_id = group->expression;
    }
    const auto* literal = std::get_if<ASTLiteral>(&ast.expression(literal_id).value);
    const auto numeric_literal = literal == nullptr ? std::nullopt : as_numeric_literal(*literal);
    if (prefix.op == ASTPrefixOperator::Negate && numeric_literal.has_value()) {
        const auto inferred =
            numeric_literal_type(module_analysis, literal->span, *numeric_literal);
        const auto resolved = expected.has_value()
                && numeric_suffix(*numeric_literal) == NumericSuffix::None
                && is_numeric(module_analysis, *expected)
                && is_numeric(module_analysis, inferred)
                && (is_integer(module_analysis, *expected) == is_integer(module_analysis, inferred))
            ? *expected
            : inferred;
        const auto fact =
            negative_literal_fact(module_analysis, literal->span, *numeric_literal, resolved);
        return append_expression(
            module_analysis,
            {
                .origin = expression_origin,
                .type = resolved,
                .constant = fact.constant,
                .value = HIRLiteralExpr {.value = fact.value},
            }
        );
    }
    const auto operand = build_expression(module_analysis, scopes, control, prefix.operand_id);
    auto op = HIRUnaryExpr::Operator::Negate;
    if (prefix.op == ASTPrefixOperator::LogicalNot) {
        op = HIRUnaryExpr::Operator::LogicalNot;
    }
    if (prefix.op == ASTPrefixOperator::BitwiseNot) {
        op = HIRUnaryExpr::Operator::BitwiseNot;
    }
    const auto& builder = module_analysis.builder();
    const auto operand_type = expression_type(module_analysis, operand);
    const auto check = check_unary_operator(builder, op, operand_type);
    switch (check.status) {
        case UnaryOperatorStatus::BooleanOperandRequired:
            module_analysis.emit(
                prefix.operator_span,
                "logical negation requires a bool operand",
                DiagnosticCode::TypePrefixBool
            );
            break;
        case UnaryOperatorStatus::NumericOperandRequired:
            module_analysis.emit(
                prefix.operator_span,
                "arithmetic negation requires a numeric operand",
                DiagnosticCode::TypePrefixNumeric
            );
            break;
        case UnaryOperatorStatus::IntegerOperandRequired:
            module_analysis.emit(
                prefix.operator_span,
                "bitwise negation requires an integer operand",
                DiagnosticCode::TypePrefixInteger
            );
            break;
        case UnaryOperatorStatus::Supported:
        case UnaryOperatorStatus::Foreign:
        case UnaryOperatorStatus::Error:     break;
    }
    const auto result_builtin = operator_result_builtin(check.result);
    const auto result_type = result_builtin.has_value()
        ? builtin(module_analysis, prefix.operator_span, *result_builtin)
        : operand_type;
    auto constant = std::optional<HIRConstant>();
    auto evaluation = evaluate_unary_constant(builder, op, operand, result_type);
    if (evaluation.has_value()) {
        constant = std::move(evaluation->value);
    } else if (evaluation.error() == HIRConstantEvaluationFailure::IntegerOverflow) {
        module_analysis
            .emit(prefix.operator_span, "integer constant overflow", DiagnosticCode::ConstOverflow);
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = result_type,
            .constant = std::move(constant),
            .value = HIRUnaryExpr {.op = op, .operand_id = operand},
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTAccessExpr& access,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto operand = build_expression(module_analysis, scopes, control, access.operand_id);
    if (access.mode == ASTAccessMode::Write) {
        module_analysis.emit(
            access.marker_span,
            "Write access is only valid as a direct Carven call argument",
            DiagnosticCode::AccessExpression
        );
        return operand;
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = expression_type(module_analysis, operand),
            .constant = std::nullopt,
            .value = HIRTakeExpr {
                .operand_id = operand,
                .marker_origin = module_analysis.origin(access.marker_span),
            },
        }
    );
}

auto build_expression(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTCastExpr& cast,
    ASTExprID,
    ProgramOriginID expression_origin
) noexcept -> HIRExprID {
    const auto& builder = module_analysis.builder();
    const auto operand = build_expression(module_analysis, scopes, control, cast.operand_id);
    const auto source_type = expression_type(module_analysis, operand);
    const auto target_type = build_type(module_analysis, scopes, control, cast.target_type);
    auto source_is_numeric_enum = false;
    if (!is_opaque_or_error(module_analysis, source_type)
        && !is_opaque_or_error(module_analysis, target_type)
        && source_type != target_type) {
        const auto enumeration_resolution =
            resolve_enum_contract(module_analysis, source_type, cast.operator_span);
        if (!enumeration_resolution.has_value()
            && enumeration_resolution.error() == LookupError::Diagnosed) {
            return module_analysis.recover_expression(cast.operator_span);
        }
        source_is_numeric_enum = enumeration_resolution.has_value()
            && module_analysis.declarations().enumeration(enumeration_resolution.value()).profile
                == HIREnumProfile::Numeric;
    }
    const auto check = check_cast(builder, source_type, target_type, source_is_numeric_enum);
    const auto valid = check.status == CastStatus::Supported;
    const auto kind = check.kind.value_or(HIRCastKind::Identity);
    if (check.status == CastStatus::Invalid) {
        module_analysis
            .emit(cast.operator_span, "invalid 'as' conversion", DiagnosticCode::TypeCast);
    }

    auto constant = std::optional<HIRConstant>();
    if (valid) {
        auto evaluation =
            evaluate_cast_constant(builder, kind, operand, target_type, source_is_numeric_enum);
        if (evaluation.has_value()) {
            constant = std::move(evaluation->value);
        }
    }
    return append_expression(
        module_analysis,
        {
            .origin = expression_origin,
            .type = valid ? target_type : error_type(module_analysis, cast.operator_span),
            .constant = std::move(constant),
            .value = HIRCastExpr {
                .operand_id = operand,
                .kind = kind,
            },
        }
    );
}
