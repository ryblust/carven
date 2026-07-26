module carven:semantic.analysis.constant.evaluate;

import :semantic.hir;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.type;
import std;

enum class HIROperatorResult {
    Operand,
    Boolean,
};

enum class HIRUnaryCapability {
    Supported,
    Foreign,
    Error,
    BooleanOperandRequired,
    NumericOperandRequired,
    IntegerOperandRequired,
};

struct HIRUnaryOperatorProfile final {
    HIRUnaryCapability capability;
    HIROperatorResult result;
};

enum class HIRBinaryCapability {
    Supported,
    Foreign,
    Error,
    BooleanOperandsRequired,
    NumericOperandsRequired,
    IntegerOperandsRequired,
};

struct HIRBinaryOperatorProfile final {
    bool compatible;
    bool equality_supported;
    HIRBinaryCapability capability;
    HIROperatorResult result;
};

enum class HIRCastCapability {
    Supported,
    Error,
    Invalid,
};

struct HIRCastOperatorProfile final {
    HIRCastCapability capability;
    std::optional<HIRCastKind> kind;
};

struct HIRTextIntrinsicProfile final {
    bool supported;
    bool constant_bearing;
    HIRBuiltinType result;
};

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

auto operator_result_builtin(HIROperatorResult profile) noexcept -> std::optional<HIRBuiltinType>;

auto unary_operator_profile(
    const SemanticConstruction& hir,
    HIRUnaryExpr::Operator op,
    HIRTypeID operand
) noexcept -> HIRUnaryOperatorProfile;

auto binary_operator_profile(
    const SemanticConstruction& hir,
    HIRBinaryExpr::Operator op,
    HIRTypeID left,
    HIRTypeID right,
    bool equality_capable
) noexcept -> HIRBinaryOperatorProfile;

auto cast_operator_profile(
    const SemanticConstruction& hir,
    HIRTypeID source,
    HIRTypeID target,
    bool source_is_numeric_enum
) noexcept -> HIRCastOperatorProfile;

auto text_intrinsic_profile(
    const SemanticConstruction& hir,
    HIRTextIntrinsic intrinsic,
    HIRTypeID operand
) noexcept -> HIRTextIntrinsicProfile;

auto evaluate_unary_constant(
    const SemanticConstruction& hir,
    HIRUnaryExpr::Operator op,
    HIRExprID operand,
    HIRTypeID result
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure>;

auto evaluate_binary_constant(
    const SemanticConstruction& hir,
    HIRBinaryExpr::Operator op,
    HIRExprID left,
    HIRExprID right,
    HIRTypeID result,
    bool equality_capable
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure>;

auto evaluate_cast_constant(
    const SemanticConstruction& hir,
    HIRCastKind kind,
    HIRExprID operand,
    HIRTypeID result,
    bool source_is_numeric_enum
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure>;

auto evaluate_text_intrinsic_constant(
    const SemanticConstruction& hir,
    HIRTextIntrinsic intrinsic,
    HIRExprID operand,
    HIRTypeID result
) noexcept -> std::expected<HIRConstantEvaluation, HIRConstantEvaluationFailure>;
