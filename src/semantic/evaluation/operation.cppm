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

// Only patterns whose selection has no execution or binding-path obligations.
template<typename Pattern>
auto known_pattern_match(
    const ExecutionValueAccess& values,
    const Pattern& pattern,
    std::optional<ConstantID> subject
) noexcept -> std::optional<bool> {
    return pattern.value.visit([&](const auto& value) noexcept -> std::optional<bool> {
        using Value = std::remove_cvref_t<decltype(value)>;
        if constexpr (std::same_as<Value, WildcardPattern>
                      || std::same_as<Value, BindingPattern>
                      || std::same_as<Value, TypeConstraintPattern>
                      || std::same_as<Value, ElaboratedTypeConstraintPattern>) {
            return true;
        } else {
            if (!subject) {
                return std::nullopt;
            }
            const auto& fact = values.constant(*subject);
            if constexpr (std::same_as<Value, LiteralPattern>) {
                return constant_value_equal(
                    values,
                    fact.value,
                    values.constant(value.constant).value
                );
            } else if constexpr (std::same_as<Value, RangePattern>) {
                if ((value.begin && !value.begin->constant)
                    || (value.end && !value.end->constant)) {
                    return std::nullopt;
                }
                const auto compare = [&](const RangePatternBound& bound,
                                         BinaryOperator operation) noexcept -> std::optional<bool> {
                    const auto result = evaluate_binary_constant_value(
                        values,
                        operation,
                        fact,
                        values.constant(*bound.constant),
                        values.builtin_type(BuiltinType::Bool)
                    );
                    if (!result) {
                        return std::nullopt;
                    }
                    const auto* truth = std::get_if<BooleanConstant>(&result->value);
                    return truth ? std::optional(truth->value) : std::nullopt;
                };
                const auto first = value.begin ? compare(*value.begin, BinaryOperator::GreaterEqual)
                                               : std::optional(true);
                const auto last = value.end
                    ? compare(
                          *value.end,
                          value.inclusive ? BinaryOperator::LessEqual : BinaryOperator::Less
                      )
                    : std::optional(true);
                return first && last ? std::optional(*first && *last) : std::nullopt;
            } else if constexpr (std::same_as<Value, EnumCasePattern>) {
                if (!value.payload.empty()) {
                    return std::nullopt;
                }
                if (const auto* numeric = std::get_if<NumericEnumConstant>(&fact.value)) {
                    return numeric->enum_case == value.enum_case;
                }
                if (const auto* payload = std::get_if<PayloadEnumConstant>(&fact.value)) {
                    return payload->enum_case == value.enum_case;
                }
            }
            return std::nullopt;
        }
    });
}
