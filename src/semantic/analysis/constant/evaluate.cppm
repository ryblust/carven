module carven:semantic.analysis.constant.evaluate;

import :diagnostics.code;
import :frontend.ast.literal;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.type;
import std;

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
};

struct ConstantEvaluationDiagnostic final {
    std::string_view message;
    DiagnosticCode code;
};

enum class LiteralSign {
    Positive,
    Negative,
};

struct NormalizedLiteral final {
    LiteralValue literal;
    ConstantFact constant;
};

auto constant_evaluation_diagnostic(ConstantEvaluationFailure failure) noexcept
    -> std::optional<ConstantEvaluationDiagnostic>;

auto load_constant_fact(const ProgramDraft& draft, std::optional<ConstantID> constant) noexcept
    -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto constant_value_equal(
    const ProgramDraft& draft,
    const ConstantValue& left,
    const ConstantValue& right
) noexcept -> bool;

auto evaluate_unary_constant_value(
    const ProgramDraft& draft,
    UnaryOperator operation,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto evaluate_binary_constant_value(
    const ProgramDraft& draft,
    BinaryOperator operation,
    const ConstantFact& left,
    const ConstantFact& right,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto evaluate_cast_constant_value(
    const ProgramDraft& draft,
    CastKind kind,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto evaluate_text_intrinsic_constant_value(
    const ProgramDraft& draft,
    TextIntrinsic intrinsic,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;

auto fold_unary_constant(
    const ProgramDraft& draft,
    UnaryOperator operation,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto fold_binary_constant(
    const ProgramDraft& draft,
    BinaryOperator operation,
    std::optional<ConstantID> left,
    std::optional<ConstantID> right,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto fold_cast_constant(
    const ProgramDraft& draft,
    CastKind kind,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
auto fold_text_intrinsic_constant(
    const ProgramDraft& draft,
    TextIntrinsic intrinsic,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;

auto normalize_literal(
    ProgramDraft& draft,
    const ASTLiteral& literal,
    std::optional<ConstructionTypeRef> expected = std::nullopt,
    LiteralSign sign = LiteralSign::Positive
) noexcept -> std::expected<NormalizedLiteral, ConstantEvaluationFailure>;
