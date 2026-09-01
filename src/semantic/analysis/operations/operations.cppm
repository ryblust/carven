module carven:semantic.analysis.operations;

import :semantic.analysis.session.read;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.type;
import std;

enum class OperatorResult {
    Operand,
    Boolean,
};

enum class UnaryOperatorStatus {
    Supported,
    Foreign,
    Error,
    BooleanOperandRequired,
    NumericOperandRequired,
    IntegerOperandRequired,
};

struct UnaryOperatorCheck final {
    UnaryOperatorStatus status;
    OperatorResult result;
};

enum class BinaryOperatorStatus {
    Supported,
    Foreign,
    Error,
    BooleanOperandsRequired,
    NumericOperandsRequired,
    IntegerOperandsRequired,
};

struct BinaryOperatorCheck final {
    bool equality_supported;
    BinaryOperatorStatus status;
    OperatorResult result;
};

enum class CastStatus {
    Supported,
    Error,
    Invalid,
};

struct CastCheck final {
    CastStatus status;
    std::optional<HIRCastKind> kind;
};

struct TextIntrinsicCheck final {
    bool supported;
    bool constant_bearing;
    HIRBuiltinType result;
};
auto operator_result_builtin(OperatorResult result) noexcept -> std::optional<HIRBuiltinType>;

auto check_unary_operator(
    SemanticDraftView hir,
    HIRUnaryExpr::Operator op,
    HIRTypeID operand
) noexcept -> UnaryOperatorCheck;

auto check_binary_operator(
    SemanticDraftView hir,
    HIRBinaryExpr::Operator op,
    HIRTypeID left,
    HIRTypeID right,
    bool equality_capable
) noexcept -> BinaryOperatorCheck;

auto check_cast(
    SemanticDraftView hir,
    HIRTypeID source,
    HIRTypeID target,
    bool source_is_numeric_enum
) noexcept -> CastCheck;

auto check_text_intrinsic(
    SemanticDraftView hir,
    HIRTextIntrinsic intrinsic,
    HIRTypeID operand
) noexcept -> TextIntrinsicCheck;
