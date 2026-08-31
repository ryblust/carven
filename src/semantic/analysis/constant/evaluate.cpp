module carven:semantic.analysis.constant.evaluate.impl;

import :semantic.analysis.session;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.elaboration.types.relations;
import :semantic.hir;
import :semantic.hir.constant;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.visit;
import std;

namespace {

auto valid_type(SemanticDraftView hir, HIRTypeID type) noexcept -> bool {
    return type.index() < hir.types().size();
}

auto valid_expression(SemanticDraftView hir, HIRExprID expression) noexcept -> bool {
    return expression.index() < hir.expressions().size();
}

auto valid_constant(SemanticDraftView hir, HIRConstantID constant) noexcept -> bool {
    return constant.index() < hir.constants().size();
}

auto builtin_type(SemanticDraftView hir, HIRTypeID type) noexcept -> std::optional<HIRBuiltinType> {
    if (!valid_type(hir, type)) {
        return std::nullopt;
    }
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&hir.type(type).value);
    return builtin == nullptr ? std::nullopt : std::optional(builtin->kind);
}

auto is_error(SemanticDraftView hir, HIRTypeID type) noexcept -> bool {
    return valid_type(hir, type) && std::holds_alternative<HIRErrorTypeValue>(hir.type(type).value);
}

auto is_foreign(SemanticDraftView hir, HIRTypeID type) noexcept -> bool {
    return valid_type(hir, type)
        && std::holds_alternative<HIRForeignTypeValue>(hir.type(type).value);
}

auto checked_add(std::int64_t left, std::int64_t right) noexcept -> std::optional<std::int64_t> {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left > maximum - right) || (right < 0 && left < minimum - right)) {
        return std::nullopt;
    }
    return left + right;
}

auto checked_subtract(std::int64_t left, std::int64_t right) noexcept
    -> std::optional<std::int64_t> {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((right > 0 && left < minimum + right) || (right < 0 && left > maximum + right)) {
        return std::nullopt;
    }
    return left - right;
}

auto checked_multiply(std::int64_t left, std::int64_t right) noexcept
    -> std::optional<std::int64_t> {
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if (left == 0 || right == 0) {
        return 0ll;
    }
    if ((left > 0 && right > 0 && left > maximum / right)
        || (left > 0 && right < 0 && right < minimum / left)
        || (left < 0 && right > 0 && left < minimum / right)
        || (left < 0 && right < 0 && left < maximum / right)) {
        return std::nullopt;
    }
    return left * right;
}

auto checked_add(std::uint64_t left, std::uint64_t right) noexcept -> std::optional<std::uint64_t> {
    if (left > std::numeric_limits<std::uint64_t>::max() - right) {
        return std::nullopt;
    }
    return left + right;
}

auto checked_subtract(std::uint64_t left, std::uint64_t right) noexcept
    -> std::optional<std::uint64_t> {
    if (left < right) {
        return std::nullopt;
    }
    return left - right;
}

auto checked_multiply(std::uint64_t left, std::uint64_t right) noexcept
    -> std::optional<std::uint64_t> {
    if (right != 0 && left > std::numeric_limits<std::uint64_t>::max() / right) {
        return std::nullopt;
    }
    return left * right;
}

auto expression_constant(SemanticDraftView hir, HIRExprID expression) noexcept
    -> std::expected<HIRConstantID, HIRConstantEvaluationFailure> {
    if (!valid_expression(hir, expression)) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto constant = hir.expression(expression).constant;
    if (!constant.has_value()) {
        return std::unexpected(HIRConstantEvaluationFailure::OperandNotConstant);
    }
    if (!valid_constant(hir, *constant)) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    return *constant;
}

auto integer_evaluation(HIRIntegerConstant value) noexcept -> HIRConstantEvaluation {
    return {
        .value = value,
    };
}

auto boolean_evaluation(bool value) noexcept -> HIRConstantEvaluation {
    return {
        .value = HIRBooleanConstant {.value = value},
    };
}

auto finish_integer(HIRIntegerConstant value, HIRBuiltinType type) noexcept
    -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure> {
    if (!integer_constant_fits(value, type)) {
        return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
    }
    return integer_evaluation(value);
}

auto evaluate_signed_binary(
    HIRBinaryExpr::Operator op,
    HIRIntegerConstant left,
    HIRIntegerConstant right,
    HIRBuiltinType type
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure> {
    const auto width = builtin_integer_width(type);
    const auto lhs = left.as_signed();
    const auto rhs = right.as_signed();
    if (!width.has_value() || !lhs.has_value() || !rhs.has_value()) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    switch (op) {
        case HIRBinaryExpr::Operator::Add:
            if (const auto result = checked_add(*lhs, *rhs)) {
                return finish_integer(HIRIntegerConstant::from_signed(*result), type);
            }
            return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
        case HIRBinaryExpr::Operator::Subtract:
            if (const auto result = checked_subtract(*lhs, *rhs)) {
                return finish_integer(HIRIntegerConstant::from_signed(*result), type);
            }
            return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
        case HIRBinaryExpr::Operator::Multiply:
            if (const auto result = checked_multiply(*lhs, *rhs)) {
                return finish_integer(HIRIntegerConstant::from_signed(*result), type);
            }
            return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
        case HIRBinaryExpr::Operator::Divide:
        case HIRBinaryExpr::Operator::Remainder:
            if (*rhs == 0) {
                return std::unexpected(HIRConstantEvaluationFailure::DivideByZero);
            }
            if (*lhs == std::numeric_limits<std::int64_t>::min() && *rhs == -1) {
                if (op == HIRBinaryExpr::Operator::Divide) {
                    return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
                }
                return finish_integer(HIRIntegerConstant::zero(), type);
            }
            return finish_integer(
                HIRIntegerConstant::from_signed(
                    op == HIRBinaryExpr::Operator::Divide ? *lhs / *rhs : *lhs % *rhs
                ),
                type
            );
        case HIRBinaryExpr::Operator::BitwiseOr:
            return finish_integer(HIRIntegerConstant::from_signed(*lhs | *rhs), type);
        case HIRBinaryExpr::Operator::BitwiseXor:
            return finish_integer(HIRIntegerConstant::from_signed(*lhs ^ *rhs), type);
        case HIRBinaryExpr::Operator::BitwiseAnd:
            return finish_integer(HIRIntegerConstant::from_signed(*lhs & *rhs), type);
        case HIRBinaryExpr::Operator::LeftShift:
            if (*rhs < 0 || static_cast<std::uint64_t>(*rhs) >= *width) {
                return std::unexpected(HIRConstantEvaluationFailure::ShiftOutOfRange);
            }
            if (*lhs < 0) {
                return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
            }
            {
                const auto shift = static_cast<std::uint8_t>(*rhs);
                const auto maximum = *width == 64
                    ? std::numeric_limits<std::int64_t>::max()
                    : static_cast<std::int64_t>((1ull << (*width - 1)) - 1);
                if (shift != 0 && *lhs > (maximum >> shift)) {
                    return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
                }
                return finish_integer(HIRIntegerConstant::from_signed(*lhs << shift), type);
            }
        case HIRBinaryExpr::Operator::RightShift:
            if (*rhs < 0 || static_cast<std::uint64_t>(*rhs) >= *width) {
                return std::unexpected(HIRConstantEvaluationFailure::ShiftOutOfRange);
            }
            return finish_integer(
                HIRIntegerConstant::from_signed(*lhs >> static_cast<std::uint8_t>(*rhs)),
                type
            );
        case HIRBinaryExpr::Operator::Less:         return boolean_evaluation(*lhs < *rhs);
        case HIRBinaryExpr::Operator::LessEqual:    return boolean_evaluation(*lhs <= *rhs);
        case HIRBinaryExpr::Operator::Greater:      return boolean_evaluation(*lhs > *rhs);
        case HIRBinaryExpr::Operator::GreaterEqual: return boolean_evaluation(*lhs >= *rhs);
        case HIRBinaryExpr::Operator::LogicalOr:
        case HIRBinaryExpr::Operator::LogicalAnd:
        case HIRBinaryExpr::Operator::Equal:
        case HIRBinaryExpr::Operator::NotEqual:
            return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    std::unreachable();
}

auto evaluate_unsigned_binary(
    HIRBinaryExpr::Operator op,
    HIRIntegerConstant left,
    HIRIntegerConstant right,
    HIRBuiltinType type
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure> {
    const auto width = builtin_integer_width(type);
    const auto lhs = left.as_unsigned();
    const auto rhs = right.as_unsigned();
    if (!width.has_value() || !lhs.has_value() || !rhs.has_value()) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto maximum =
        *width == 64 ? std::numeric_limits<std::uint64_t>::max() : (1ull << *width) - 1;
    switch (op) {
        case HIRBinaryExpr::Operator::Add:
            if (const auto result = checked_add(*lhs, *rhs)) {
                return finish_integer(HIRIntegerConstant::from_parts(*result, false), type);
            }
            return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
        case HIRBinaryExpr::Operator::Subtract:
            if (const auto result = checked_subtract(*lhs, *rhs)) {
                return finish_integer(HIRIntegerConstant::from_parts(*result, false), type);
            }
            return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
        case HIRBinaryExpr::Operator::Multiply:
            if (const auto result = checked_multiply(*lhs, *rhs)) {
                return finish_integer(HIRIntegerConstant::from_parts(*result, false), type);
            }
            return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
        case HIRBinaryExpr::Operator::Divide:
        case HIRBinaryExpr::Operator::Remainder:
            if (*rhs == 0) {
                return std::unexpected(HIRConstantEvaluationFailure::DivideByZero);
            }
            return finish_integer(
                HIRIntegerConstant::from_parts(
                    op == HIRBinaryExpr::Operator::Divide ? *lhs / *rhs : *lhs % *rhs,
                    false
                ),
                type
            );
        case HIRBinaryExpr::Operator::BitwiseOr:
            return finish_integer(HIRIntegerConstant::from_parts(*lhs | *rhs, false), type);
        case HIRBinaryExpr::Operator::BitwiseXor:
            return finish_integer(HIRIntegerConstant::from_parts(*lhs ^ *rhs, false), type);
        case HIRBinaryExpr::Operator::BitwiseAnd:
            return finish_integer(HIRIntegerConstant::from_parts(*lhs & *rhs, false), type);
        case HIRBinaryExpr::Operator::LeftShift:
            if (*rhs >= *width) {
                return std::unexpected(HIRConstantEvaluationFailure::ShiftOutOfRange);
            }
            if (*rhs != 0 && *lhs > (maximum >> *rhs)) {
                return std::unexpected(HIRConstantEvaluationFailure::IntegerOverflow);
            }
            return finish_integer(HIRIntegerConstant::from_parts(*lhs << *rhs, false), type);
        case HIRBinaryExpr::Operator::RightShift:
            if (*rhs >= *width) {
                return std::unexpected(HIRConstantEvaluationFailure::ShiftOutOfRange);
            }
            return finish_integer(HIRIntegerConstant::from_parts(*lhs >> *rhs, false), type);
        case HIRBinaryExpr::Operator::Less:         return boolean_evaluation(*lhs < *rhs);
        case HIRBinaryExpr::Operator::LessEqual:    return boolean_evaluation(*lhs <= *rhs);
        case HIRBinaryExpr::Operator::Greater:      return boolean_evaluation(*lhs > *rhs);
        case HIRBinaryExpr::Operator::GreaterEqual: return boolean_evaluation(*lhs >= *rhs);
        case HIRBinaryExpr::Operator::LogicalOr:
        case HIRBinaryExpr::Operator::LogicalAnd:
        case HIRBinaryExpr::Operator::Equal:
        case HIRBinaryExpr::Operator::NotEqual:
            return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    std::unreachable();
}

} // namespace

auto operator_result_builtin(HIROperatorResult profile) noexcept -> std::optional<HIRBuiltinType> {
    switch (profile) {
        case HIROperatorResult::Operand: return std::nullopt;
        case HIROperatorResult::Boolean: return HIRBuiltinType::Bool;
    }
    std::unreachable();
}

auto hir_operator_result_matches(
    SemanticDraftView hir,
    HIROperatorResult profile,
    HIRTypeID operand,
    HIRTypeID result
) noexcept -> bool {
    if (!valid_type(hir, operand) || !valid_type(hir, result)) {
        return false;
    }
    const auto expected_builtin = operator_result_builtin(profile);
    return expected_builtin.has_value() ? builtin_type(hir, result) == *expected_builtin
                                        : operand == result;
}

auto unary_operator_profile(
    SemanticDraftView hir,
    HIRUnaryExpr::Operator op,
    HIRTypeID operand
) noexcept -> HIRUnaryOperatorProfile {
    const auto result = op == HIRUnaryExpr::Operator::LogicalNot ? HIROperatorResult::Boolean
                                                                 : HIROperatorResult::Operand;
    if (!valid_type(hir, operand) || is_error(hir, operand)) {
        return {.capability = HIRUnaryCapability::Error, .result = result};
    }
    if (is_foreign(hir, operand)) {
        return {.capability = HIRUnaryCapability::Foreign, .result = result};
    }
    const auto builtin = builtin_type(hir, operand);
    switch (op) {
        case HIRUnaryExpr::Operator::LogicalNot:
            return {
                .capability = builtin == HIRBuiltinType::Bool
                    ? HIRUnaryCapability::Supported
                    : HIRUnaryCapability::BooleanOperandRequired,
                .result = result,
            };
        case HIRUnaryExpr::Operator::Negate:
            return {
                .capability = builtin.has_value() && builtin_is_numeric(*builtin)
                    ? HIRUnaryCapability::Supported
                    : HIRUnaryCapability::NumericOperandRequired,
                .result = result,
            };
        case HIRUnaryExpr::Operator::BitwiseNot:
            return {
                .capability = builtin.has_value() && builtin_is_integer(*builtin)
                    ? HIRUnaryCapability::Supported
                    : HIRUnaryCapability::IntegerOperandRequired,
                .result = result,
            };
    }
    std::unreachable();
}

auto binary_operator_profile(
    SemanticDraftView hir,
    HIRBinaryExpr::Operator op,
    HIRTypeID left,
    HIRTypeID right,
    bool equality_capable
) noexcept -> HIRBinaryOperatorProfile {
    const auto compatible = type_compatible(hir, left, right);
    const auto boolean_result = op == HIRBinaryExpr::Operator::LogicalOr
        || op == HIRBinaryExpr::Operator::LogicalAnd
        || op == HIRBinaryExpr::Operator::Equal
        || op == HIRBinaryExpr::Operator::NotEqual
        || op == HIRBinaryExpr::Operator::Less
        || op == HIRBinaryExpr::Operator::LessEqual
        || op == HIRBinaryExpr::Operator::Greater
        || op == HIRBinaryExpr::Operator::GreaterEqual;
    const auto result = boolean_result ? HIROperatorResult::Boolean : HIROperatorResult::Operand;
    const auto equality =
        op == HIRBinaryExpr::Operator::Equal || op == HIRBinaryExpr::Operator::NotEqual;
    if (!valid_type(hir, left)
        || !valid_type(hir, right)
        || is_error(hir, left)
        || is_error(hir, right)) {
        return {
            .compatible = compatible,
            .equality_supported = !equality || equality_capable,
            .capability = HIRBinaryCapability::Error,
            .result = result,
        };
    }
    if (is_foreign(hir, left) || is_foreign(hir, right)) {
        return {
            .compatible = compatible,
            .equality_supported = !equality || equality_capable,
            .capability = HIRBinaryCapability::Foreign,
            .result = result,
        };
    }
    const auto left_builtin = builtin_type(hir, left);
    const auto right_builtin = builtin_type(hir, right);
    auto capability = HIRBinaryCapability::Supported;
    switch (op) {
        case HIRBinaryExpr::Operator::LogicalOr:
        case HIRBinaryExpr::Operator::LogicalAnd:
            if (left_builtin != HIRBuiltinType::Bool || right_builtin != HIRBuiltinType::Bool) {
                capability = HIRBinaryCapability::BooleanOperandsRequired;
            }
            break;
        case HIRBinaryExpr::Operator::Add:
        case HIRBinaryExpr::Operator::Subtract:
        case HIRBinaryExpr::Operator::Multiply:
        case HIRBinaryExpr::Operator::Divide:
        case HIRBinaryExpr::Operator::Less:
        case HIRBinaryExpr::Operator::LessEqual:
        case HIRBinaryExpr::Operator::Greater:
        case HIRBinaryExpr::Operator::GreaterEqual:
            if (!left_builtin.has_value()
                || !right_builtin.has_value()
                || !builtin_is_numeric(*left_builtin)
                || !builtin_is_numeric(*right_builtin)) {
                capability = HIRBinaryCapability::NumericOperandsRequired;
            }
            break;
        case HIRBinaryExpr::Operator::BitwiseOr:
        case HIRBinaryExpr::Operator::BitwiseXor:
        case HIRBinaryExpr::Operator::BitwiseAnd:
        case HIRBinaryExpr::Operator::LeftShift:
        case HIRBinaryExpr::Operator::RightShift:
        case HIRBinaryExpr::Operator::Remainder:
            if (!left_builtin.has_value()
                || !right_builtin.has_value()
                || !builtin_is_integer(*left_builtin)
                || !builtin_is_integer(*right_builtin)) {
                capability = HIRBinaryCapability::IntegerOperandsRequired;
            }
            break;
        case HIRBinaryExpr::Operator::Equal:
        case HIRBinaryExpr::Operator::NotEqual: break;
    }
    return {
        .compatible = compatible,
        .equality_supported = !equality || equality_capable,
        .capability = capability,
        .result = result,
    };
}

auto cast_operator_profile(
    SemanticDraftView hir,
    HIRTypeID source,
    HIRTypeID target,
    bool source_is_numeric_enum
) noexcept -> HIRCastOperatorProfile {
    if (!valid_type(hir, source)
        || !valid_type(hir, target)
        || is_error(hir, source)
        || is_error(hir, target)) {
        return {.capability = HIRCastCapability::Error, .kind = std::nullopt};
    }
    if (is_foreign(hir, source) || is_foreign(hir, target)) {
        return {.capability = HIRCastCapability::Invalid, .kind = std::nullopt};
    }
    if (source == target) {
        return {
            .capability = HIRCastCapability::Supported,
            .kind = HIRCastKind::Identity,
        };
    }
    const auto source_builtin = builtin_type(hir, source);
    const auto target_builtin = builtin_type(hir, target);
    const auto source_integer = source_builtin.has_value() && builtin_is_integer(*source_builtin);
    const auto target_integer = target_builtin.has_value() && builtin_is_integer(*target_builtin);
    auto kind = std::optional<HIRCastKind>();
    if (source_integer && target_integer) {
        kind = HIRCastKind::IntegerToInteger;
    } else if (source_integer && target_builtin == HIRBuiltinType::Bool) {
        kind = HIRCastKind::IntegerToBool;
    } else if (source_builtin == HIRBuiltinType::Bool && target_integer) {
        kind = HIRCastKind::BoolToInteger;
    } else if (source_integer
               && (target_builtin == HIRBuiltinType::F32
                   || target_builtin == HIRBuiltinType::F64)) {
        kind = HIRCastKind::IntegerToFloating;
    } else if (source_builtin == HIRBuiltinType::F32 && target_builtin == HIRBuiltinType::F64) {
        kind = HIRCastKind::FloatingWiden;
    } else if (source_is_numeric_enum && target_integer) {
        kind = HIRCastKind::EnumToInteger;
    }
    return {
        .capability = kind.has_value() ? HIRCastCapability::Supported : HIRCastCapability::Invalid,
        .kind = kind,
    };
}

auto text_intrinsic_profile(
    SemanticDraftView hir,
    HIRTextIntrinsic intrinsic,
    HIRTypeID operand
) noexcept -> HIRTextIntrinsicProfile {
    auto result = HIRBuiltinType::Usize;
    auto constant_bearing = true;
    switch (intrinsic) {
        case HIRTextIntrinsic::Len:     result = HIRBuiltinType::Usize; break;
        case HIRTextIntrinsic::IsEmpty: result = HIRBuiltinType::Bool; break;
        case HIRTextIntrinsic::Bytes:
            result = HIRBuiltinType::StrBytesView;
            constant_bearing = false;
            break;
        case HIRTextIntrinsic::Chars:
            result = HIRBuiltinType::StrCharsView;
            constant_bearing = false;
            break;
    }
    return {
        .supported = builtin_type(hir, operand) == HIRBuiltinType::Str,
        .constant_bearing = constant_bearing,
        .result = result,
    };
}

auto hir_type_supports_equality(SemanticDraftView hir, HIRTypeID type) noexcept -> bool {
    if (!valid_type(hir, type)) {
        return false;
    }
    if (std::holds_alternative<HIRForeignTypeValue>(hir.type(type).value)) {
        return true;
    }
    const auto check = [&](this const auto& self, HIRTypeID candidate) noexcept -> bool {
        if (!valid_type(hir, candidate)) {
            return false;
        }
        const auto& value = hir.type(candidate).value;
        if (std::holds_alternative<HIRErrorTypeValue>(value)) {
            return true;
        }
        if (std::holds_alternative<HIRForeignTypeValue>(value)) {
            return false;
        }
        if (const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&value)) {
            return builtin_type_supports_equality(builtin->kind);
        }
        if (const auto* array = std::get_if<HIRArrayTypeValue>(&value)) {
            return self(array->element_type_id);
        }
        if (const auto* structure = std::get_if<HIRStructTypeValue>(&value)) {
            return structure->structure.index() < hir.structures().size()
                && hir.nominal_capabilities(HIRNominalDeclRef {structure->structure}).equality;
        }
        if (const auto* enumeration = std::get_if<HIREnumTypeValue>(&value)) {
            return enumeration->enumeration.index() < hir.enumerations().size()
                && hir.nominal_capabilities(HIRNominalDeclRef {enumeration->enumeration}).equality;
        }
        return false;
    };
    return check(type);
}

auto hir_type_is_numeric_enum(SemanticDraftView hir, HIRTypeID type) noexcept -> bool {
    if (!valid_type(hir, type)) {
        return false;
    }
    const auto* nominal = std::get_if<HIREnumTypeValue>(&hir.type(type).value);
    if (nominal == nullptr || nominal->enumeration.index() >= hir.enumerations().size()) {
        return false;
    }
    return hir.enumeration(nominal->enumeration).profile == HIREnumProfile::Numeric;
}

auto evaluate_unary_constant(
    SemanticDraftView hir,
    HIRUnaryExpr::Operator op,
    HIRExprID operand,
    HIRTypeID result
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure> {
    if (!valid_expression(hir, operand)) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto& operand_expression = hir.expression(operand);
    const auto profile = unary_operator_profile(hir, op, operand_expression.type);
    if (!hir_operator_result_matches(hir, profile.result, operand_expression.type, result)) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    if (profile.capability == HIRUnaryCapability::Foreign) {
        return std::unexpected(HIRConstantEvaluationFailure::UnsupportedOperation);
    }
    if (profile.capability != HIRUnaryCapability::Supported) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto constant = expression_constant(hir, operand);
    if (!constant.has_value()) {
        return std::unexpected(constant.error());
    }
    const auto& fact = hir.constant(*constant);
    switch (op) {
        case HIRUnaryExpr::Operator::LogicalNot:
            if (const auto* boolean = std::get_if<HIRBooleanConstant>(&fact.value)) {
                return boolean_evaluation(!boolean->value);
            }
            return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
        case HIRUnaryExpr::Operator::Negate:
            if (const auto* integer = std::get_if<HIRIntegerConstant>(&fact.value)) {
                const auto type = builtin_type(hir, operand_expression.type);
                if (!type.has_value()) {
                    return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
                }
                const auto value =
                    HIRIntegerConstant::from_parts(integer->magnitude(), !integer->negative());
                return finish_integer(value, *type);
            }
            return std::unexpected(HIRConstantEvaluationFailure::UnsupportedOperation);
        case HIRUnaryExpr::Operator::BitwiseNot:
            if (const auto* integer = std::get_if<HIRIntegerConstant>(&fact.value)) {
                const auto type = builtin_type(hir, operand_expression.type);
                if (!type.has_value()) {
                    return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
                }
                const auto width = builtin_integer_width(*type);
                if (!width.has_value()) {
                    return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
                }
                if (builtin_is_signed_integer(*type)) {
                    const auto value = integer->as_signed();
                    if (!value.has_value()) {
                        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
                    }
                    return finish_integer(HIRIntegerConstant::from_signed(~*value), *type);
                }
                const auto mask =
                    *width == 64 ? std::numeric_limits<std::uint64_t>::max() : (1ull << *width) - 1;
                return finish_integer(
                    HIRIntegerConstant::from_parts((~integer->magnitude()) & mask, false),
                    *type
                );
            }
            return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    std::unreachable();
}

auto evaluate_binary_constant(
    SemanticDraftView hir,
    HIRBinaryExpr::Operator op,
    HIRExprID left,
    HIRExprID right,
    HIRTypeID result,
    bool equality_capable
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure> {
    if (!valid_expression(hir, left) || !valid_expression(hir, right)) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto& left_expression = hir.expression(left);
    const auto& right_expression = hir.expression(right);
    const auto profile = binary_operator_profile(
        hir,
        op,
        left_expression.type,
        right_expression.type,
        equality_capable
    );
    if (!profile.compatible
        || !profile.equality_supported
        || !hir_operator_result_matches(hir, profile.result, left_expression.type, result)) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    if (profile.capability == HIRBinaryCapability::Foreign) {
        return std::unexpected(HIRConstantEvaluationFailure::UnsupportedOperation);
    }
    if (profile.capability != HIRBinaryCapability::Supported) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto left_constant = expression_constant(hir, left);
    if (!left_constant.has_value()) {
        return std::unexpected(left_constant.error());
    }
    const auto right_constant = expression_constant(hir, right);
    if (!right_constant.has_value()) {
        return std::unexpected(right_constant.error());
    }
    if (op == HIRBinaryExpr::Operator::Equal || op == HIRBinaryExpr::Operator::NotEqual) {
        const auto equal = constant_equal(hir, *left_constant, *right_constant);
        return boolean_evaluation(op == HIRBinaryExpr::Operator::Equal ? equal : !equal);
    }
    const auto& left_fact = hir.constant(*left_constant);
    const auto& right_fact = hir.constant(*right_constant);
    if (const auto* left_integer = std::get_if<HIRIntegerConstant>(&left_fact.value)) {
        const auto* right_integer = std::get_if<HIRIntegerConstant>(&right_fact.value);
        const auto type = builtin_type(hir, left_expression.type);
        if (right_integer == nullptr || !type.has_value() || !builtin_is_integer(*type)) {
            return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
        }
        return builtin_is_signed_integer(*type)
            ? evaluate_signed_binary(op, *left_integer, *right_integer, *type)
            : evaluate_unsigned_binary(op, *left_integer, *right_integer, *type);
    }
    const auto* left_boolean = std::get_if<HIRBooleanConstant>(&left_fact.value);
    const auto* right_boolean = std::get_if<HIRBooleanConstant>(&right_fact.value);
    if (left_boolean != nullptr && right_boolean != nullptr) {
        if (op == HIRBinaryExpr::Operator::LogicalAnd) {
            return boolean_evaluation(left_boolean->value && right_boolean->value);
        }
        if (op == HIRBinaryExpr::Operator::LogicalOr) {
            return boolean_evaluation(left_boolean->value || right_boolean->value);
        }
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    return std::unexpected(HIRConstantEvaluationFailure::UnsupportedOperation);
}

auto evaluate_cast_constant(
    SemanticDraftView hir,
    HIRCastKind kind,
    HIRExprID operand,
    HIRTypeID result,
    bool source_is_numeric_enum
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure> {
    if (!valid_expression(hir, operand) || !valid_type(hir, result)) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto& operand_expression = hir.expression(operand);
    const auto profile =
        cast_operator_profile(hir, operand_expression.type, result, source_is_numeric_enum);
    if (profile.capability != HIRCastCapability::Supported || profile.kind != kind) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto constant = expression_constant(hir, operand);
    if (!constant.has_value()) {
        return std::unexpected(constant.error());
    }
    const auto& fact = hir.constant(*constant);
    if (kind == HIRCastKind::Identity) {
        return HIRConstantEvaluation {
            .value = fact.value,
        };
    }
    const auto target = builtin_type(hir, result);
    if (!target.has_value()) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    if (const auto* integer = std::get_if<HIRIntegerConstant>(&fact.value)) {
        if (kind == HIRCastKind::IntegerToBool) {
            return boolean_evaluation(integer->magnitude() != 0);
        }
        if (kind == HIRCastKind::IntegerToInteger) {
            return integer_evaluation(normalize_integer_cast(*integer, *target));
        }
        if (kind == HIRCastKind::IntegerToFloating) {
            auto value = integer->negative() ? -static_cast<double>(integer->magnitude())
                                             : static_cast<double>(integer->magnitude());
            if (*target == HIRBuiltinType::F32) {
                value = static_cast<double>(static_cast<float>(value));
            }
            return HIRConstantEvaluation {
                .value = HIRFloatingConstant {.value = value},
            };
        }
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    if (const auto* boolean = std::get_if<HIRBooleanConstant>(&fact.value)) {
        if (kind != HIRCastKind::BoolToInteger) {
            return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
        }
        return integer_evaluation(normalize_integer_cast(
            HIRIntegerConstant::from_parts(boolean->value ? 1ull : 0ull, false),
            *target
        ));
    }
    if (const auto* enumeration = std::get_if<HIRNumericEnumConstant>(&fact.value)) {
        if (kind != HIRCastKind::EnumToInteger) {
            return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
        }
        return integer_evaluation(normalize_integer_cast(enumeration->value, *target));
    }
    if (kind == HIRCastKind::FloatingWiden
        && std::holds_alternative<HIRFloatingConstant>(fact.value)) {
        return HIRConstantEvaluation {
            .value = fact.value,
        };
    }
    return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
}

auto evaluate_text_intrinsic_constant(
    SemanticDraftView hir,
    HIRTextIntrinsic intrinsic,
    HIRExprID operand,
    HIRTypeID result
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure> {
    if (!valid_expression(hir, operand)) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto& operand_expression = hir.expression(operand);
    const auto profile = text_intrinsic_profile(hir, intrinsic, operand_expression.type);
    if (!profile.supported || builtin_type(hir, result) != profile.result) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    if (!profile.constant_bearing) {
        return std::unexpected(HIRConstantEvaluationFailure::UnsupportedOperation);
    }
    const auto constant = expression_constant(hir, operand);
    if (!constant.has_value()) {
        return std::unexpected(constant.error());
    }
    const auto* string = std::get_if<HIRStringConstant>(&hir.constant(*constant).value);
    if (string == nullptr || string->value.index() >= hir.provenance().spellings().size()) {
        return std::unexpected(HIRConstantEvaluationFailure::InvalidOperation);
    }
    const auto bytes = hir.provenance().spelling(string->value);
    if (intrinsic == HIRTextIntrinsic::IsEmpty) {
        return boolean_evaluation(bytes.empty());
    }
    if (intrinsic == HIRTextIntrinsic::Len) {
        return integer_evaluation(HIRIntegerConstant::from_parts(bytes.size(), false));
    }
    return std::unexpected(HIRConstantEvaluationFailure::UnsupportedOperation);
}
