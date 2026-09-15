module carven:semantic.evaluation.operation;

import :diagnostics.code;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.type;
import std;

enum class IntegerArithmetic { Checked, Wrapping };

enum class ConstantEvaluationFailure {
    OperandNotConstant,
    UnsupportedOperation,
    InvalidOperation,
    IntegerOverflow,
    DivideByZero,
    ShiftOutOfRange,
    IntegerLiteralOutOfRange,
    FloatingLiteralOutOfRange,
    IntegerLiteralNotRepresentable,
    SliceOutOfBounds,
};

struct ConstantEvaluationDiagnostic final {
    std::string_view message;
    DiagnosticCode code;
};

auto constant_evaluation_diagnostic(ConstantEvaluationFailure failure) noexcept
    -> std::optional<ConstantEvaluationDiagnostic>;

auto load_constant_fact(
    const ExecutionValueAccess& values,
    std::optional<ConstantID> constant
) noexcept -> std::expected<const ConstantFact*, ConstantEvaluationFailure>;
auto constant_value_equal(
    const ConstantValueReader& values,
    const ConstantValue& left,
    const ConstantValue& right
) noexcept -> bool;

auto evaluate_unary_constant_value(
    const ExecutionValueAccess& values,
    UnaryOperator operation,
    const ConstantFact& operand,
    TypeID result,
    IntegerArithmetic arithmetic = IntegerArithmetic::Checked
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto evaluate_binary_constant_value(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    const ConstantFact& left,
    const ConstantFact& right,
    TypeID result,
    IntegerArithmetic arithmetic = IntegerArithmetic::Checked
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto evaluate_cast_constant_value(
    const ExecutionValueAccess& values,
    CastKind kind,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto evaluate_text_intrinsic_constant_value(
    const ExecutionValueAccess& values,
    TextIntrinsic intrinsic,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto evaluate_slice_intrinsic_constant_value(
    const ExecutionValueAccess& values,
    SliceIntrinsic intrinsic,
    const ConstantFact& operand,
    std::span<const ConstantFact> bounds,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;

auto fold_unary_constant(
    const ExecutionValueAccess& values,
    UnaryOperator operation,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto fold_binary_constant(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    std::optional<ConstantID> left,
    std::optional<ConstantID> right,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto fold_cast_constant(
    const ExecutionValueAccess& values,
    CastKind kind,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto fold_text_intrinsic_constant(
    const ExecutionValueAccess& values,
    TextIntrinsic intrinsic,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
