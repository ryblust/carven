module carven:semantic.analysis.constant.evaluate;

import :semantic.analysis.session.read;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.type;
import std;

enum class HIRConstantEvaluationFailure {
    OperandNotConstant,
    UnsupportedOperation,
    InvalidOperation,
    IntegerOverflow,
    DivideByZero,
    ShiftOutOfRange,
};

struct HIRConstantEvaluation final {
    HIRConstant value;
};
auto evaluate_unary_constant(
    SemanticDraftView hir,
    HIRUnaryExpr::Operator op,
    HIRExprID operand,
    HIRTypeID result
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure>;
auto evaluate_binary_constant(
    SemanticDraftView hir,
    HIRBinaryExpr::Operator op,
    HIRExprID left,
    HIRExprID right,
    HIRTypeID result,
    bool operands_compatible,
    bool equality_capable
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure>;

auto evaluate_cast_constant(
    SemanticDraftView hir,
    HIRCastKind kind,
    HIRExprID operand,
    HIRTypeID result,
    bool source_is_numeric_enum
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure>;

auto evaluate_text_intrinsic_constant(
    SemanticDraftView hir,
    HIRTextIntrinsic intrinsic,
    HIRExprID operand,
    HIRTypeID result
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure>;
