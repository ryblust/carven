module carven:semantic.evaluation.operation;

import :diagnostics.code;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.simd;
import :semantic.semir.type;
import :semantic.semir.values;
import std;

// Advances a checked integer range without stepping beyond an inclusive end.
class IntegerRangeCursor final {
public:
    explicit IntegerRangeCursor(RangeConstant range) noexcept;
    auto next() noexcept -> std::optional<IntegerConstant>;

private:
    std::optional<IntegerConstant> current;
    IntegerConstant end;
    bool inclusive;
};

enum class IntegerArithmetic { Checked, Wrapping };

auto parse_number_constant(
    const ExecutionValueAccess& values,
    BuiltinType number,
    std::string_view text
) noexcept -> std::optional<ConstantAtom>;

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
    SIMDOutOfBounds,
    InvalidClampBounds,
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

// Operands have passed semantic admission for language equality.
auto constant_value_equal(
    const ConstantValueReader& values,
    const ConstantValue& left,
    const ConstantValue& right
) noexcept -> bool;

auto constant_value_equal(
    const ConstantValueReader& values,
    const ConstantAtomValue& left,
    const ConstantAtomValue& right
) noexcept -> bool;

// Operands belong to this semantic program and have passed type admission.
// These operations check execution-dependent failures, not fact storage again.
auto evaluate_unary_constant_value(
    const ExecutionValueAccess& values,
    UnaryOperator operation,
    const ConstantAtom& operand,
    TypeID result,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure>;

auto evaluate_binary_constant_value(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    const ConstantAtom& left,
    const ConstantAtom& right,
    TypeID result,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure>;

auto evaluate_cast_constant_value(
    const ExecutionValueAccess& values,
    CastKind kind,
    const ConstantAtom& operand,
    TypeID result
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure>;

auto evaluate_text_intrinsic_constant_value(
    const ExecutionValueAccess& values,
    TextIntrinsic intrinsic,
    const ConstantAtom& operand,
    TypeID result
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure>;

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
                        *constant_atom(fact),
                        *constant_atom(values.constant(*bound.constant)),
                        values.builtin_type(BuiltinType::Bool),
                        IntegerArithmetic::Wrapping
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
            } else if constexpr (std::same_as<Value, TuplePattern>) {
                return value.elements.empty() ? std::optional(true) : std::nullopt;
            } else if constexpr (std::same_as<Value, EnumCasePattern>) {
                if (!value.payload.empty()) {
                    return std::nullopt;
                }
                if (const auto* numeric = std::get_if<NumericEnumConstant>(&fact.value)) {
                    return numeric->enum_case == concrete_enum_case(value.enum_case);
                }
                if (const auto* payload = std::get_if<PayloadEnumConstant>(&fact.value)) {
                    return payload->enum_case == concrete_enum_case(value.enum_case);
                }
            }
            return std::nullopt;
        }
    });
}

auto evaluate_simd_constant_value(
    const ExecutionValueAccess& values,
    SIMDIntrinsic intrinsic,
    BuiltinType owner,
    std::span<const ConstantAtom> inputs,
    TypeID result
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure>;

auto fold_simd_constant(
    const ExecutionValueAccess& values,
    SIMDIntrinsic intrinsic,
    BuiltinType owner,
    std::span<const std::optional<ConstantID>> operands,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;

auto evaluate_float_intrinsic_constant_value(
    const ExecutionValueAccess& values,
    FloatIntrinsic intrinsic,
    std::span<const ConstantAtom> operands,
    TypeID result
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure>;
