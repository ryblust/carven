module carven:semantic.analysis.operations.impl;

import :semantic.analysis.operations;
import :semantic.hir.expr;
import :semantic.hir.type;
import std;

namespace {

auto valid_type(SemanticDraftView hir, HIRTypeID type) noexcept -> bool {
    return type.index() < hir.types().size();
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

} // namespace

auto operator_result_builtin(OperatorResult result) noexcept -> std::optional<HIRBuiltinType> {
    switch (result) {
        case OperatorResult::Operand: return std::nullopt;
        case OperatorResult::Boolean: return HIRBuiltinType::Bool;
    }
    std::unreachable();
}
auto check_unary_operator(
    SemanticDraftView hir,
    HIRUnaryExpr::Operator op,
    HIRTypeID operand
) noexcept -> UnaryOperatorCheck {
    const auto result = op == HIRUnaryExpr::Operator::LogicalNot ? OperatorResult::Boolean
                                                                 : OperatorResult::Operand;
    if (!valid_type(hir, operand) || is_error(hir, operand)) {
        return {.status = UnaryOperatorStatus::Error, .result = result};
    }
    const auto builtin = builtin_type(hir, operand);
    switch (op) {
        case HIRUnaryExpr::Operator::LogicalNot:
            return {
                .status = builtin == HIRBuiltinType::Bool
                    ? UnaryOperatorStatus::Supported
                    : UnaryOperatorStatus::BooleanOperandRequired,
                .result = result,
            };
        case HIRUnaryExpr::Operator::Negate:
            return {
                .status = builtin.has_value() && builtin_is_numeric(*builtin)
                    ? UnaryOperatorStatus::Supported
                    : UnaryOperatorStatus::NumericOperandRequired,
                .result = result,
            };
        case HIRUnaryExpr::Operator::BitwiseNot:
            return {
                .status = builtin.has_value() && builtin_is_integer(*builtin)
                    ? UnaryOperatorStatus::Supported
                    : UnaryOperatorStatus::IntegerOperandRequired,
                .result = result,
            };
    }
    std::unreachable();
}

auto check_binary_operator(
    SemanticDraftView hir,
    HIRBinaryExpr::Operator op,
    HIRTypeID left,
    HIRTypeID right,
    bool equality_capable
) noexcept -> BinaryOperatorCheck {
    const auto boolean_result = op == HIRBinaryExpr::Operator::LogicalOr
        || op == HIRBinaryExpr::Operator::LogicalAnd
        || op == HIRBinaryExpr::Operator::Equal
        || op == HIRBinaryExpr::Operator::NotEqual
        || op == HIRBinaryExpr::Operator::Less
        || op == HIRBinaryExpr::Operator::LessEqual
        || op == HIRBinaryExpr::Operator::Greater
        || op == HIRBinaryExpr::Operator::GreaterEqual;
    const auto result = boolean_result ? OperatorResult::Boolean : OperatorResult::Operand;
    const auto equality =
        op == HIRBinaryExpr::Operator::Equal || op == HIRBinaryExpr::Operator::NotEqual;
    if (!valid_type(hir, left)
        || !valid_type(hir, right)
        || is_error(hir, left)
        || is_error(hir, right)) {
        return {
            .equality_supported = !equality || equality_capable,
            .status = BinaryOperatorStatus::Error,
            .result = result,
        };
    }
    const auto left_builtin = builtin_type(hir, left);
    const auto right_builtin = builtin_type(hir, right);
    auto status = BinaryOperatorStatus::Supported;
    switch (op) {
        case HIRBinaryExpr::Operator::LogicalOr:
        case HIRBinaryExpr::Operator::LogicalAnd:
            if (left_builtin != HIRBuiltinType::Bool || right_builtin != HIRBuiltinType::Bool) {
                status = BinaryOperatorStatus::BooleanOperandsRequired;
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
                status = BinaryOperatorStatus::NumericOperandsRequired;
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
                status = BinaryOperatorStatus::IntegerOperandsRequired;
            }
            break;
        case HIRBinaryExpr::Operator::Equal:
        case HIRBinaryExpr::Operator::NotEqual: break;
    }
    return {
        .equality_supported = !equality || equality_capable,
        .status = status,
        .result = result,
    };
}

auto check_cast(
    SemanticDraftView hir,
    HIRTypeID source,
    HIRTypeID target,
    bool source_is_numeric_enum
) noexcept -> CastCheck {
    if (!valid_type(hir, source)
        || !valid_type(hir, target)
        || is_error(hir, source)
        || is_error(hir, target)) {
        return {.status = CastStatus::Error, .kind = std::nullopt};
    }
    if (source == target) {
        return {
            .status = CastStatus::Supported,
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
        .status = kind.has_value() ? CastStatus::Supported : CastStatus::Invalid,
        .kind = kind,
    };
}

auto check_text_intrinsic(
    SemanticDraftView hir,
    HIRTextIntrinsic intrinsic,
    HIRTypeID operand
) noexcept -> TextIntrinsicCheck {
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
