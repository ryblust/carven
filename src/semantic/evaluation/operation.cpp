module carven:semantic.evaluation.operation.impl;

import :diagnostics.code;
import :semantic.evaluation.operation;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.operation;
import :semantic.semir.simd;
import :semantic.semir.type;
import :support.invariant;
import std;

IntegerRangeCursor::IntegerRangeCursor(RangeConstant range) noexcept
    : current(range.begin),
      end(range.end),
      inclusive(range.inclusive) {}

auto IntegerRangeCursor::next() noexcept -> std::optional<IntegerConstant> {
    if (!current) {
        return std::nullopt;
    }
    const auto value = *current;
    const auto less = value.negative() != end.negative() ? value.negative()
        : value.negative()                               ? value.magnitude() > end.magnitude()
                                                         : value.magnitude() < end.magnitude();
    if (!less && !(inclusive && value == end)) {
        current.reset();
        return std::nullopt;
    }
    if (value == end) {
        current.reset();
    } else {
        current = value.negative() ? IntegerConstant::from_parts(value.magnitude() - 1u, true)
                                   : IntegerConstant::from_parts(value.magnitude() + 1u, false);
    }
    return value;
}

auto constant_evaluation_diagnostic(ConstantEvaluationFailure failure) noexcept
    -> std::optional<ConstantEvaluationDiagnostic> {
    switch (failure) {
        case ConstantEvaluationFailure::IntegerOverflow:
            return ConstantEvaluationDiagnostic {
                .message = "integer overflow",
                .code = DiagnosticCode::ConstOverflow,
            };
        case ConstantEvaluationFailure::DivideByZero:
            return ConstantEvaluationDiagnostic {
                .message = "division by zero in expression",
                .code = DiagnosticCode::ConstDivideByZero,
            };
        case ConstantEvaluationFailure::ShiftOutOfRange:
            return ConstantEvaluationDiagnostic {
                .message = "shift count is outside the integer type width",
                .code = DiagnosticCode::ConstShiftRange,
            };
        case ConstantEvaluationFailure::IntegerLiteralOutOfRange:
            return ConstantEvaluationDiagnostic {
                .message = "integer is out of range",
                .code = DiagnosticCode::ConstOverflow,
            };
        case ConstantEvaluationFailure::FloatingLiteralOutOfRange:
            return ConstantEvaluationDiagnostic {
                .message = "floating value is out of range",
                .code = DiagnosticCode::ConstOverflow,
            };
        case ConstantEvaluationFailure::IntegerLiteralNotRepresentable:
            return ConstantEvaluationDiagnostic {
                .message = "integer literal is not representable in its type",
                .code = DiagnosticCode::ConstLiteralRange,
            };
        case ConstantEvaluationFailure::SIMDOutOfBounds:
            return ConstantEvaluationDiagnostic {
                .message = "SIMD index or memory range is out of bounds",
                .code = DiagnosticCode::ConstIndexBounds
            };
        case ConstantEvaluationFailure::SliceOutOfBounds:
            return ConstantEvaluationDiagnostic {
                .message = "slice range is out of bounds",
                .code = DiagnosticCode::ConstIndexBounds,
            };
        case ConstantEvaluationFailure::OperandNotConstant:
        case ConstantEvaluationFailure::UnsupportedOperation:
        case ConstantEvaluationFailure::InvalidOperation:     return std::nullopt;
    }
    std::unreachable();
}

namespace {

auto builtin_type(const ExecutionValueAccess& values, TypeID type) noexcept
    -> std::optional<BuiltinType> {
    const auto canonical = values.type_copy(type);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    return builtin == nullptr ? std::nullopt : std::optional(builtin->kind);
}

auto constant_pointer_narrows(
    const ExecutionValueAccess& values,
    TypeID source,
    TypeID target
) noexcept -> bool {
    const auto source_type = values.type_copy(source);
    const auto target_type = values.type_copy(target);
    const auto* from = std::get_if<PointerTypeValue>(&source_type.value);
    const auto* to = std::get_if<PointerTypeValue>(&target_type.value);
    return from != nullptr && to != nullptr && pointer_narrows(*from, *to);
}

auto validate_constant_value(const ConstantValueReader& values, const ConstantValue& value) noexcept
    -> void {
    value.visit([&](const auto& item) noexcept {
        using Item = std::remove_cvref_t<decltype(item)>;
        if constexpr (std::same_as<Item, StringConstant> || std::same_as<Item, CStringConstant>) {
            if (!values.owns(item.value)) {
                invariant_violation("evaluator received a foreign spelling");
            }
            if constexpr (std::same_as<Item, CStringConstant>) {
                if (!valid_cstring_bytes(values.spelling(item.value))) {
                    invariant_violation("evaluator received invalid C string bytes");
                }
            }
        } else if constexpr (std::same_as<Item, NumericEnumConstant>) {
            if (item.enum_case.owner() != values.identity()) {
                invariant_violation("evaluator received a foreign enum case");
            }
        } else if constexpr (std::same_as<Item, PayloadEnumConstant>) {
            if (item.enum_case.owner() != values.identity()) {
                invariant_violation("evaluator received a foreign enum case");
            }
            for (const auto child : item.payload) {
                static_cast<void>(values.constant(child));
            }
        } else if constexpr (std::same_as<Item, StructConstant>) {
            for (const auto child : item.fields) {
                static_cast<void>(values.constant(child));
            }
        } else if constexpr (std::same_as<Item, ArrayConstant>
                             || std::same_as<Item, SliceConstant>) {
            for (const auto child : item.elements) {
                static_cast<void>(values.constant(child));
            }
        } else {
            static_assert(
                std::same_as<Item, RangeConstant>
                    || std::same_as<Item, IntegerConstant>
                    || std::same_as<Item, SIMDConstant>
                    || std::same_as<Item, BooleanConstant>
                    || std::same_as<Item, NullPointerConstant>
                    || std::same_as<Item, F32Constant>
                    || std::same_as<Item, F64Constant>
                    || std::same_as<Item, CharacterConstant>,
                "unhandled value validation"
            );
        }
    });
}

auto validate_constant_fact(const ExecutionValueAccess& values, const ConstantFact& fact) noexcept
    -> void {
    validate_constant_value(values, fact.value);
    const auto matches = constant_matches_type(
        fact,
        [&](TypeID id) noexcept -> std::optional<CanonicalType> { return values.type_copy(id); },
        [&](ConstantID id) noexcept -> std::optional<ConstantFact> { return values.constant(id); },
        [&](StructID id) noexcept { return values.struct_field_types(id); },
        [&](EnumID owner, EnumCaseID id) noexcept -> std::optional<std::vector<TypeID>> {
            const auto entries = values.enum_case_types(owner);
            if (!entries) {
                return std::nullopt;
            }
            for (const auto& entry : *entries) {
                if (entry.id == id) {
                    return entry.payload_types;
                }
            }
            return std::nullopt;
        },
        [&](ProgramSpellingID id) noexcept -> std::optional<std::string_view> {
            return values.owns(id) ? std::optional(values.spelling(id)) : std::nullopt;
        }
    );
    if (!matches) {
        invariant_violation("evaluator received a value with a mismatched type");
    }
}

auto is_builtin(const ExecutionValueAccess& values, TypeID type, BuiltinType expected) noexcept
    -> bool {
    return builtin_type(values, type) == expected;
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

auto constant_integer(TypeID type, IntegerConstant value) noexcept -> ConstantFact {
    return {.type = type, .value = value};
}

auto constant_boolean(const ExecutionValueAccess& values, TypeID type, bool value) noexcept
    -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    if (!is_builtin(values, type, BuiltinType::Bool)) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    return ConstantFact {.type = type, .value = BooleanConstant {.value = value}};
}

auto finish_constant_integer(
    const ExecutionValueAccess& values,
    TypeID result,
    IntegerConstant value,
    BuiltinType type,
    IntegerArithmetic arithmetic = IntegerArithmetic::Checked
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    if (builtin_type(values, result) != type) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    if (arithmetic == IntegerArithmetic::Wrapping) {
        const auto width = *builtin_integer_width(type);
        const auto mask = width == 64u ? std::numeric_limits<std::uint64_t>::max()
                                       : (std::uint64_t {1u} << width) - 1u;
        const auto bits = (value.negative() ? 0u - value.magnitude() : value.magnitude()) & mask;
        const auto negative =
            builtin_is_signed_integer(type) && (bits & (std::uint64_t {1u} << (width - 1u))) != 0u;
        value = IntegerConstant::from_parts(negative ? ((~bits) & mask) + 1u : bits, negative);
    }
    if (!integer_constant_fits(value, type)) {
        return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
    }
    return constant_integer(result, value);
}

template<typename Floating>
auto evaluate_floating(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    Floating left,
    Floating right,
    TypeID result,
    TypeID operand_type
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto finish =
        [&](auto value) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
        if (result != operand_type) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return ConstantFact {.type = result, .value = Floating {.value = value}};
    };
    switch (operation) {
        case BinaryOperator::Add:      return finish(left.value + right.value);
        case BinaryOperator::Subtract: return finish(left.value - right.value);
        case BinaryOperator::Multiply: return finish(left.value * right.value);
        case BinaryOperator::Divide:   return finish(left.value / right.value);
        case BinaryOperator::Less:
            return constant_boolean(values, result, left.value < right.value);
        case BinaryOperator::LessEqual:
            return constant_boolean(values, result, left.value <= right.value);
        case BinaryOperator::Greater:
            return constant_boolean(values, result, left.value > right.value);
        case BinaryOperator::GreaterEqual:
            return constant_boolean(values, result, left.value >= right.value);
        default: return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
}

auto evaluate_signed_integer(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    IntegerConstant left,
    IntegerConstant right,
    BuiltinType type,
    TypeID result,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto width = builtin_integer_width(type);
    const auto lhs = left.as_signed();
    const auto rhs = right.as_signed();
    if (!width.has_value() || !lhs.has_value() || !rhs.has_value()) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto finish = [&](IntegerConstant value) noexcept {
        return finish_constant_integer(values, result, value, type, arithmetic);
    };
    switch (operation) {
        case BinaryOperator::Add:
            if (const auto value = checked_add(*lhs, *rhs)) {
                return finish(IntegerConstant::from_signed(*value));
            }
            return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
        case BinaryOperator::Subtract:
            if (const auto value = checked_subtract(*lhs, *rhs)) {
                return finish(IntegerConstant::from_signed(*value));
            }
            return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
        case BinaryOperator::Multiply:
            if (const auto value = checked_multiply(*lhs, *rhs)) {
                return finish(IntegerConstant::from_signed(*value));
            }
            return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
        case BinaryOperator::Divide:
        case BinaryOperator::Remainder:
            if (*rhs == 0) {
                return std::unexpected(ConstantEvaluationFailure::DivideByZero);
            }
            if (*lhs == std::numeric_limits<std::int64_t>::min() && *rhs == -1) {
                if (operation == BinaryOperator::Divide) {
                    if (arithmetic == IntegerArithmetic::Wrapping) {
                        return finish(left);
                    }
                    return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
                }
                return finish(IntegerConstant::zero());
            }
            return finish(
                IntegerConstant::from_signed(
                    operation == BinaryOperator::Divide ? *lhs / *rhs : *lhs % *rhs
                )
            );
        case BinaryOperator::BitwiseOr:  return finish(IntegerConstant::from_signed(*lhs | *rhs));
        case BinaryOperator::BitwiseXor: return finish(IntegerConstant::from_signed(*lhs ^ *rhs));
        case BinaryOperator::BitwiseAnd: return finish(IntegerConstant::from_signed(*lhs & *rhs));
        case BinaryOperator::LeftShift:
            if (*rhs < 0 || static_cast<std::uint64_t>(*rhs) >= *width) {
                return std::unexpected(ConstantEvaluationFailure::ShiftOutOfRange);
            }
            if (*lhs < 0) {
                return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
            }
            {
                const auto shift = static_cast<std::uint8_t>(*rhs);
                const auto maximum = *width == 64u
                    ? std::numeric_limits<std::int64_t>::max()
                    : static_cast<std::int64_t>((std::uint64_t {1u} << (*width - 1u)) - 1u);
                if (shift != 0u && *lhs > (maximum >> shift)) {
                    return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
                }
                return finish(IntegerConstant::from_signed(*lhs << shift));
            }
        case BinaryOperator::RightShift:
            if (*rhs < 0 || static_cast<std::uint64_t>(*rhs) >= *width) {
                return std::unexpected(ConstantEvaluationFailure::ShiftOutOfRange);
            }
            return finish(IntegerConstant::from_signed(*lhs >> static_cast<std::uint8_t>(*rhs)));
        case BinaryOperator::Less:         return constant_boolean(values, result, *lhs < *rhs);
        case BinaryOperator::LessEqual:    return constant_boolean(values, result, *lhs <= *rhs);
        case BinaryOperator::Greater:      return constant_boolean(values, result, *lhs > *rhs);
        case BinaryOperator::GreaterEqual: return constant_boolean(values, result, *lhs >= *rhs);
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual:
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    std::unreachable();
}

auto evaluate_unsigned_integer(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    IntegerConstant left,
    IntegerConstant right,
    BuiltinType type,
    TypeID result,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto width = builtin_integer_width(type);
    const auto lhs = left.as_unsigned();
    const auto rhs = right.as_unsigned();
    if (!width.has_value() || !lhs.has_value() || !rhs.has_value()) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto maximum = *width == 64u ? std::numeric_limits<std::uint64_t>::max()
                                       : (std::uint64_t {1u} << *width) - 1u;
    const auto finish = [&](IntegerConstant value) noexcept {
        return finish_constant_integer(values, result, value, type, arithmetic);
    };
    switch (operation) {
        case BinaryOperator::Add:
            if (const auto value = checked_add(*lhs, *rhs)) {
                return finish(IntegerConstant::from_parts(*value, false));
            }
            return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
        case BinaryOperator::Subtract:
            if (const auto value = checked_subtract(*lhs, *rhs)) {
                return finish(IntegerConstant::from_parts(*value, false));
            }
            return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
        case BinaryOperator::Multiply:
            if (const auto value = checked_multiply(*lhs, *rhs)) {
                return finish(IntegerConstant::from_parts(*value, false));
            }
            return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
        case BinaryOperator::Divide:
        case BinaryOperator::Remainder:
            if (*rhs == 0u) {
                return std::unexpected(ConstantEvaluationFailure::DivideByZero);
            }
            return finish(
                IntegerConstant::from_parts(
                    operation == BinaryOperator::Divide ? *lhs / *rhs : *lhs % *rhs,
                    false
                )
            );
        case BinaryOperator::BitwiseOr:
            return finish(IntegerConstant::from_parts(*lhs | *rhs, false));
        case BinaryOperator::BitwiseXor:
            return finish(IntegerConstant::from_parts(*lhs ^ *rhs, false));
        case BinaryOperator::BitwiseAnd:
            return finish(IntegerConstant::from_parts(*lhs & *rhs, false));
        case BinaryOperator::LeftShift:
            if (*rhs >= *width) {
                return std::unexpected(ConstantEvaluationFailure::ShiftOutOfRange);
            }
            if (*rhs != 0u && *lhs > (maximum >> *rhs)) {
                return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
            }
            return finish(IntegerConstant::from_parts(*lhs << *rhs, false));
        case BinaryOperator::RightShift:
            if (*rhs >= *width) {
                return std::unexpected(ConstantEvaluationFailure::ShiftOutOfRange);
            }
            return finish(IntegerConstant::from_parts(*lhs >> *rhs, false));
        case BinaryOperator::Less:         return constant_boolean(values, result, *lhs < *rhs);
        case BinaryOperator::LessEqual:    return constant_boolean(values, result, *lhs <= *rhs);
        case BinaryOperator::Greater:      return constant_boolean(values, result, *lhs > *rhs);
        case BinaryOperator::GreaterEqual: return constant_boolean(values, result, *lhs >= *rhs);
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual:
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    std::unreachable();
}

} // namespace

auto load_constant_fact(
    const ExecutionValueAccess& values,
    std::optional<ConstantID> constant
) noexcept -> std::expected<const ConstantFact*, ConstantEvaluationFailure> {
    if (!constant.has_value()) {
        return std::unexpected(ConstantEvaluationFailure::OperandNotConstant);
    }
    const auto& fact = values.constant(*constant);
    validate_constant_fact(values, fact);
    return &fact;
}

auto constant_value_equal(
    const ConstantValueReader& values,
    const ConstantValue& left,
    const ConstantValue& right
) noexcept -> bool {
    validate_constant_value(values, left);
    validate_constant_value(values, right);
    if (left.index() != right.index()) {
        return false;
    }
    return left.visit([&](const auto& left_value) noexcept -> bool {
        using Value = std::remove_cvref_t<decltype(left_value)>;
        const auto& right_value = std::get<Value>(right);
        if constexpr (std::same_as<Value, PayloadEnumConstant>) {
            if (left_value.enum_case != right_value.enum_case
                || left_value.payload.size() != right_value.payload.size()) {
                return false;
            }
            for (const auto [left_child, right_child] :
                 std::views::zip(left_value.payload, right_value.payload)) {
                const auto& left_fact = values.constant(left_child);
                const auto& right_fact = values.constant(right_child);
                if (left_fact.type != right_fact.type
                    || !constant_value_equal(values, left_fact.value, right_fact.value)) {
                    return false;
                }
            }
            return true;
        } else if constexpr (std::same_as<Value, StructConstant>) {
            invariant_violation("structure constants do not support language equality");
        } else if constexpr (std::same_as<Value, ArrayConstant>) {
            if (left_value.elements.size() != right_value.elements.size()) {
                return false;
            }
            for (const auto [left_child, right_child] :
                 std::views::zip(left_value.elements, right_value.elements)) {
                const auto& left_fact = values.constant(left_child);
                const auto& right_fact = values.constant(right_child);
                if (left_fact.type != right_fact.type
                    || !constant_value_equal(values, left_fact.value, right_fact.value)) {
                    return false;
                }
            }
            return true;
        } else if constexpr (std::same_as<Value, F32Constant> || std::same_as<Value, F64Constant>) {
            return left_value.value == right_value.value;
        } else {
            return left_value == right_value;
        }
    });
}

auto evaluate_unary_constant_value(
    const ExecutionValueAccess& values,
    UnaryOperator operation,
    const ConstantFact& operand,
    TypeID result,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(values, operand);
    static_cast<void>(values.type_copy(result));
    if (const auto* vector = std::get_if<SIMDConstant>(&operand.value)) {
        const auto owner = builtin_type(values, operand.type);
        const auto layout = owner ? simd_layout(*owner) : std::nullopt;
        if (!layout || operand.type != result) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        const auto floating = *owner == layout->vector && layout->element == BuiltinType::F32;
        if (operation != (floating ? UnaryOperator::Negate : UnaryOperator::BitwiseNot)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        auto output = *vector;
        for (auto& lane : output.lanes) {
            lane = floating ? lane ^ 0x80000000u : (~lane & 255u);
        }
        return ConstantFact {.type = result, .value = std::move(output)};
    }
    switch (operation) {
        case UnaryOperator::LogicalNot:
            if (const auto* boolean = std::get_if<BooleanConstant>(&operand.value);
                boolean != nullptr && is_builtin(values, operand.type, BuiltinType::Bool)) {
                return constant_boolean(values, result, !boolean->value);
            }
            break;
        case UnaryOperator::Negate:
            if (const auto* floating = std::get_if<F32Constant>(&operand.value)) {
                if (result != operand.type) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                return ConstantFact {
                    .type = result,
                    .value = F32Constant {.value = -floating->value}
                };
            }
            if (const auto* floating = std::get_if<F64Constant>(&operand.value)) {
                if (result != operand.type) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                return ConstantFact {
                    .type = result,
                    .value = F64Constant {.value = -floating->value}
                };
            }
            if (const auto* integer = std::get_if<IntegerConstant>(&operand.value)) {
                const auto type = builtin_type(values, operand.type);
                if (type.has_value() && builtin_is_integer(*type)) {
                    return finish_constant_integer(
                        values,
                        result,
                        IntegerConstant::from_parts(integer->magnitude(), !integer->negative()),
                        *type,
                        arithmetic
                    );
                }
            }
            break;
        case UnaryOperator::BitwiseNot:
            if (const auto* integer = std::get_if<IntegerConstant>(&operand.value)) {
                const auto type = builtin_type(values, operand.type);
                const auto width = type.has_value() ? builtin_integer_width(*type) : std::nullopt;
                if (!type.has_value() || !width.has_value()) {
                    break;
                }
                if (builtin_is_signed_integer(*type)) {
                    const auto value = integer->as_signed();
                    if (value.has_value()) {
                        return finish_constant_integer(
                            values,
                            result,
                            IntegerConstant::from_signed(~*value),
                            *type
                        );
                    }
                    break;
                }
                const auto mask = *width == 64u ? std::numeric_limits<std::uint64_t>::max()
                                                : (std::uint64_t {1u} << *width) - 1u;
                return finish_constant_integer(
                    values,
                    result,
                    IntegerConstant::from_parts((~integer->magnitude()) & mask, false),
                    *type
                );
            }
            break;
    }
    return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
}

auto evaluate_binary_constant_value(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    const ConstantFact& left,
    const ConstantFact& right,
    TypeID result,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(values, left);
    validate_constant_fact(values, right);
    static_cast<void>(values.type_copy(result));
    if (const auto* first = std::get_if<SIMDConstant>(&left.value)) {
        const auto* second = std::get_if<SIMDConstant>(&right.value);
        const auto owner = builtin_type(values, left.type);
        const auto layout = owner ? simd_layout(*owner) : std::nullopt;
        if (!second || !layout || left.type != right.type) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        if (*owner == layout->mask
            && operation != BinaryOperator::BitwiseAnd
            && operation != BinaryOperator::BitwiseOr
            && operation != BinaryOperator::BitwiseXor) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        const auto comparison = operation == BinaryOperator::Equal
            || operation == BinaryOperator::NotEqual
            || operation == BinaryOperator::Less
            || operation == BinaryOperator::LessEqual
            || operation == BinaryOperator::Greater
            || operation == BinaryOperator::GreaterEqual;
        if (result != (comparison ? values.builtin_type(layout->mask) : left.type)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        auto output = SIMDConstant {.lanes = std::vector<std::uint32_t>(layout->width)};
        const auto floating = *owner == layout->vector && layout->element == BuiltinType::F32;
        for (auto i = 0uz; i < output.lanes.size(); ++i) {
            const auto a = first->lanes[i];
            const auto b = second->lanes[i];
            if (floating) {
                const auto x = std::bit_cast<float>(a);
                const auto y = std::bit_cast<float>(b);
                auto predicate = false;
                switch (operation) {
                    case BinaryOperator::Add:
                        output.lanes[i] = std::bit_cast<std::uint32_t>(x + y);
                        break;
                    case BinaryOperator::Subtract:
                        output.lanes[i] = std::bit_cast<std::uint32_t>(x - y);
                        break;
                    case BinaryOperator::Multiply:
                        output.lanes[i] = std::bit_cast<std::uint32_t>(x * y);
                        break;
                    case BinaryOperator::Divide:
                        output.lanes[i] = std::bit_cast<std::uint32_t>(x / y);
                        break;
                    case BinaryOperator::Equal:        predicate = x == y; break;
                    case BinaryOperator::NotEqual:     predicate = x != y; break;
                    case BinaryOperator::Less:         predicate = x < y; break;
                    case BinaryOperator::LessEqual:    predicate = x <= y; break;
                    case BinaryOperator::Greater:      predicate = x > y; break;
                    case BinaryOperator::GreaterEqual: predicate = x >= y; break;
                    default: return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                if (comparison) {
                    output.lanes[i] = predicate ? 255u : 0u;
                }
            } else {
                switch (operation) {
                    case BinaryOperator::Add:          output.lanes[i] = (a + b) & 255u; break;
                    case BinaryOperator::Subtract:     output.lanes[i] = (a - b) & 255u; break;
                    case BinaryOperator::BitwiseAnd:   output.lanes[i] = a & b; break;
                    case BinaryOperator::BitwiseOr:    output.lanes[i] = a | b; break;
                    case BinaryOperator::BitwiseXor:   output.lanes[i] = a ^ b; break;
                    case BinaryOperator::Equal:        output.lanes[i] = a == b ? 255u : 0u; break;
                    case BinaryOperator::NotEqual:     output.lanes[i] = a != b ? 255u : 0u; break;
                    case BinaryOperator::Less:         output.lanes[i] = a < b ? 255u : 0u; break;
                    case BinaryOperator::LessEqual:    output.lanes[i] = a <= b ? 255u : 0u; break;
                    case BinaryOperator::Greater:      output.lanes[i] = a > b ? 255u : 0u; break;
                    case BinaryOperator::GreaterEqual: output.lanes[i] = a >= b ? 255u : 0u; break;
                    default: return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
            }
        }
        return ConstantFact {.type = result, .value = std::move(output)};
    }
    if (operation == BinaryOperator::Equal || operation == BinaryOperator::NotEqual) {
        if (std::holds_alternative<SliceConstant>(left.value)) {
            return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
        }
        const auto equal = constant_value_equal(values, left.value, right.value);
        return constant_boolean(
            values,
            result,
            operation == BinaryOperator::Equal ? equal : !equal
        );
    }
    if (const auto* floating = std::get_if<F32Constant>(&left.value)) {
        const auto* other = std::get_if<F32Constant>(&right.value);
        if (other == nullptr) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return evaluate_floating(values, operation, *floating, *other, result, left.type);
    }
    if (const auto* floating = std::get_if<F64Constant>(&left.value)) {
        const auto* other = std::get_if<F64Constant>(&right.value);
        if (other == nullptr) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return evaluate_floating(values, operation, *floating, *other, result, left.type);
    }
    if (const auto* left_integer = std::get_if<IntegerConstant>(&left.value)) {
        const auto* right_integer = std::get_if<IntegerConstant>(&right.value);
        const auto type = builtin_type(values, left.type);
        if (right_integer == nullptr || !type.has_value() || !builtin_is_integer(*type)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        if (arithmetic == IntegerArithmetic::Wrapping) {
            const auto bits = [](IntegerConstant value) static noexcept {
                return value.negative() ? 0u - value.magnitude() : value.magnitude();
            };
            const auto finish = [&](std::uint64_t value) noexcept {
                return finish_constant_integer(
                    values,
                    result,
                    IntegerConstant::from_parts(value, false),
                    *type,
                    arithmetic
                );
            };
            switch (operation) {
                case BinaryOperator::Add: return finish(bits(*left_integer) + bits(*right_integer));
                case BinaryOperator::Subtract:
                    return finish(bits(*left_integer) - bits(*right_integer));
                case BinaryOperator::Multiply:
                    return finish(bits(*left_integer) * bits(*right_integer));
                case BinaryOperator::LeftShift:
                    if (right_integer->negative()
                        || right_integer->magnitude() >= *builtin_integer_width(*type)) {
                        return std::unexpected(ConstantEvaluationFailure::ShiftOutOfRange);
                    }
                    return finish(bits(*left_integer) << right_integer->magnitude());
                default: break;
            }
        }
        return builtin_is_signed_integer(*type) ? evaluate_signed_integer(
                                                      values,
                                                      operation,
                                                      *left_integer,
                                                      *right_integer,
                                                      *type,
                                                      result,
                                                      arithmetic
                                                  )
                                                : evaluate_unsigned_integer(
                                                      values,
                                                      operation,
                                                      *left_integer,
                                                      *right_integer,
                                                      *type,
                                                      result,
                                                      arithmetic
                                                  );
    }
    return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
}

auto evaluate_cast_constant_value(
    const ExecutionValueAccess& values,
    CastKind kind,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(values, operand);
    const auto target = builtin_type(values, result);
    if (kind == CastKind::PointerRead
        && std::holds_alternative<NullPointerConstant>(operand.value)) {
        if (constant_pointer_narrows(values, operand.type, result)) {
            return ConstantFact {.type = result, .value = NullPointerConstant {}};
        }
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    if (kind == CastKind::Identity) {
        if (operand.type != result) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return ConstantFact {.type = result, .value = operand.value};
    }
    if (!target.has_value()) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto source = values.type_copy(operand.type);
    const auto* source_builtin = std::get_if<BuiltinTypeValue>(&source.value);
    if (const auto* integer = std::get_if<IntegerConstant>(&operand.value)) {
        if (source_builtin == nullptr || !builtin_is_integer(source_builtin->kind)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        if (kind == CastKind::IntegerToBool && *target == BuiltinType::Bool) {
            return constant_boolean(values, result, integer->magnitude() != 0u);
        }
        if (kind == CastKind::IntegerToInteger && builtin_is_integer(*target)) {
            return constant_integer(result, normalize_integer_cast(*integer, *target));
        }
        if (kind == CastKind::IntegerToFloating
            && (*target == BuiltinType::F32 || *target == BuiltinType::F64)) {
            const auto convert = [&]<typename Floating>() noexcept -> ConstantFact {
                using Native = decltype(Floating::value);
                const auto value = integer->negative() ? static_cast<Native>(*integer->as_signed())
                                                       : static_cast<Native>(integer->magnitude());
                return ConstantFact {.type = result, .value = Floating {.value = value}};
            };
            return *target == BuiltinType::F32 ? convert.template operator()<F32Constant>()
                                               : convert.template operator()<F64Constant>();
        }
    }
    if (const auto* character = std::get_if<CharacterConstant>(&operand.value)) {
        if (kind == CastKind::CharToU32
            && source_builtin != nullptr
            && source_builtin->kind == BuiltinType::Char
            && *target == BuiltinType::U32) {
            return constant_integer(result, IntegerConstant::from_parts(character->scalar, false));
        }
    }
    if (const auto* boolean = std::get_if<BooleanConstant>(&operand.value)) {
        if (kind == CastKind::BoolToInteger
            && source_builtin != nullptr
            && source_builtin->kind == BuiltinType::Bool
            && builtin_is_integer(*target)) {
            return constant_integer(
                result,
                normalize_integer_cast(
                    IntegerConstant::from_parts(boolean->value ? 1u : 0u, false),
                    *target
                )
            );
        }
    }
    if (const auto* enumeration = std::get_if<NumericEnumConstant>(&operand.value)) {
        if (kind == CastKind::EnumToInteger
            && std::holds_alternative<EnumTypeValue>(source.value)
            && builtin_is_integer(*target)) {
            return constant_integer(result, normalize_integer_cast(enumeration->value, *target));
        }
    }
    if (kind == CastKind::FloatingWiden && *target == BuiltinType::F64) {
        if (const auto* value = std::get_if<F32Constant>(&operand.value); value != nullptr
            && source_builtin != nullptr
            && source_builtin->kind == BuiltinType::F32) {
            return ConstantFact {
                .type = result,
                .value = F64Constant {.value = static_cast<double>(value->value)},
            };
        }
    }
    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
}

auto evaluate_text_intrinsic_constant_value(
    const ExecutionValueAccess& values,
    TextIntrinsic intrinsic,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(values, operand);
    const auto* string = std::get_if<StringConstant>(&operand.value);
    if (string == nullptr || !is_builtin(values, operand.type, BuiltinType::Str)) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    const auto bytes = values.spelling(string->value);
    switch (intrinsic) {
        case TextIntrinsic::Len: {
            const auto target = builtin_type(values, result);
            if (!target.has_value() || *target != BuiltinType::Usize) {
                return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
            }
            return finish_constant_integer(
                values,
                result,
                IntegerConstant::from_parts(static_cast<std::uint64_t>(bytes.size()), false),
                *target
            );
        }
        case TextIntrinsic::IsEmpty: return constant_boolean(values, result, bytes.empty());
        case TextIntrinsic::FromStr:
        case TextIntrinsic::FromUTF8Unchecked:
        case TextIntrinsic::FromU32Unchecked:
        case TextIntrinsic::AsStr:
        case TextIntrinsic::Append:
        case TextIntrinsic::Push:
        case TextIntrinsic::Clear:
        case TextIntrinsic::Bytes:
        case TextIntrinsic::Chars:
            return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    std::unreachable();
}

auto evaluate_slice_intrinsic_constant_value(
    const ExecutionValueAccess& values,
    SliceIntrinsic intrinsic,
    const ConstantFact& operand,
    std::span<const ConstantFact> bounds,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(values, operand);
    const auto* slice = std::get_if<SliceConstant>(&operand.value);
    if (slice == nullptr || intrinsic == SliceIntrinsic::FromArray) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    if (bounds.size() != (intrinsic == SliceIntrinsic::Slice ? 2uz : 0uz)) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    if (intrinsic == SliceIntrinsic::IsEmpty) {
        return constant_boolean(values, result, slice->elements.empty());
    }
    if (intrinsic == SliceIntrinsic::Len) {
        if (!is_builtin(values, result, BuiltinType::Usize)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return ConstantFact {
            .type = result,
            .value = IntegerConstant::from_parts(slice->elements.size(), false),
        };
    }
    if (result != operand.type) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    auto offsets = std::array<std::uint64_t, 2> {};
    for (auto index = 0uz; index < bounds.size(); ++index) {
        const auto& bound = bounds[index];
        validate_constant_fact(values, bound);
        const auto* integer = std::get_if<IntegerConstant>(&bound.value);
        if (!is_builtin(values, bound.type, BuiltinType::Usize) || integer == nullptr) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        offsets[index] = integer->magnitude();
    }
    const auto [start, end] = offsets;
    if (start > end || end > slice->elements.size()) {
        return std::unexpected(ConstantEvaluationFailure::SliceOutOfBounds);
    }
    const auto selected = std::span(slice->elements).subspan(start, end - start);
    return ConstantFact {
        .type = result,
        .value = SliceConstant {.elements = std::vector(selected.begin(), selected.end())},
    };
}

auto fold_unary_constant(
    const ExecutionValueAccess& values,
    UnaryOperator operation,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto fact = load_constant_fact(values, operand);
    if (!fact.has_value()) {
        return std::unexpected(fact.error());
    }
    // Runtime floating expressions retain the native target's execution environment.
    if (std::holds_alternative<F32Constant>((*fact)->value)
        || std::holds_alternative<F64Constant>((*fact)->value)
        || (std::holds_alternative<SIMDConstant>((*fact)->value)
            && (builtin_type(values, (*fact)->type) == BuiltinType::F32x4
                || builtin_type(values, (*fact)->type) == BuiltinType::F32x8))) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    return evaluate_unary_constant_value(values, operation, **fact, result);
}

auto fold_binary_constant(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    std::optional<ConstantID> left,
    std::optional<ConstantID> right,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto left_fact = load_constant_fact(values, left);
    if (!left_fact.has_value()) {
        return std::unexpected(left_fact.error());
    }
    const auto right_fact = load_constant_fact(values, right);
    if (!right_fact.has_value()) {
        return std::unexpected(right_fact.error());
    }
    if (operation != BinaryOperator::Equal
        && operation != BinaryOperator::NotEqual
        && (std::holds_alternative<F32Constant>((*left_fact)->value)
            || std::holds_alternative<F64Constant>((*left_fact)->value)
            || (std::holds_alternative<SIMDConstant>((*left_fact)->value)
                && (builtin_type(values, (*left_fact)->type) == BuiltinType::F32x4
                    || builtin_type(values, (*left_fact)->type) == BuiltinType::F32x8))
            || (std::holds_alternative<SIMDConstant>((*right_fact)->value)
                && (builtin_type(values, (*right_fact)->type) == BuiltinType::F32x4
                    || builtin_type(values, (*right_fact)->type) == BuiltinType::F32x8)))) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    return evaluate_binary_constant_value(values, operation, **left_fact, **right_fact, result);
}

auto fold_cast_constant(
    const ExecutionValueAccess& values,
    CastKind kind,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto fact = load_constant_fact(values, operand);
    if (!fact.has_value()) {
        return std::unexpected(fact.error());
    }
    return evaluate_cast_constant_value(values, kind, **fact, result);
}

auto fold_text_intrinsic_constant(
    const ExecutionValueAccess& values,
    TextIntrinsic intrinsic,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto fact = load_constant_fact(values, operand);
    if (!fact.has_value()) {
        return std::unexpected(fact.error());
    }
    return evaluate_text_intrinsic_constant_value(values, intrinsic, **fact, result);
}

auto evaluate_simd_constant_value(
    const ExecutionValueAccess& values,
    SIMDIntrinsic intrinsic,
    BuiltinType owner,
    std::span<const ConstantFact> inputs,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto contract = simd_contract(intrinsic, owner);
    if (inputs.size() != contract.inputs.size()) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto lookup = [&](TypeID id) noexcept {
        return values.type_copy(id);
    };
    for (const auto& [index, input] : std::views::enumerate(inputs)) {
        validate_constant_fact(values, input);
        if (!matches_simd_type(contract.inputs[index], input.type, lookup)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
    }
    if (!matches_simd_type(contract.result, result, lookup)) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto layout = *simd_layout(owner);
    const auto width = layout.width;
    const auto floating = layout.element == BuiltinType::F32;
    using Vector = SIMDConstant;
    using Mask = SIMDConstant;
    const auto scalar = [&](const ConstantFact& fact) noexcept -> std::uint32_t {
        return floating
            ? std::bit_cast<std::uint32_t>(std::get<F32Constant>(fact.value).value)
            : static_cast<std::uint32_t>(std::get<IntegerConstant>(fact.value).magnitude());
    };
    const auto scalar_fact = [&](std::uint32_t bits) noexcept -> ConstantFact {
        if (floating) {
            return {.type = result, .value = F32Constant {.value = std::bit_cast<float>(bits)}};
        }
        return {.type = result, .value = IntegerConstant::from_parts(bits, false)};
    };
    const auto number = [&](std::size_t index) noexcept {
        return std::get<IntegerConstant>(inputs[index].value).magnitude();
    };
    const auto lanes = [&](std::size_t index) noexcept -> const auto& {
        return std::get<Vector>(inputs[index].value).lanes;
    };
    const auto mask = [&](std::size_t index) noexcept -> const auto& {
        return std::get<Mask>(inputs[index].value).lanes;
    };
    auto output = Vector {.lanes = std::vector<std::uint32_t>(width)};
    switch (intrinsic) {
        case SIMDIntrinsic::Splat:    std::ranges::fill(output.lanes, scalar(inputs[0])); break;
        case SIMDIntrinsic::FromBits:
        case SIMDIntrinsic::Prefix:   {
            auto output_mask = Mask {.lanes = std::vector<std::uint32_t>(width)};
            if (intrinsic == SIMDIntrinsic::Prefix && number(0) > width) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            for (auto i = 0uz; i < width; ++i) {
                const auto active = intrinsic == SIMDIntrinsic::Prefix
                    ? i < number(0)
                    : (number(0) & (1ull << i)) != 0;
                output_mask.lanes[i] = active ? 255 : 0;
            }
            return ConstantFact {.type = result, .value = std::move(output_mask)};
        }
        case SIMDIntrinsic::Lane:
            if (number(1) >= width) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            return scalar_fact(lanes(0)[number(1)]);
        case SIMDIntrinsic::WithLane:
            if (number(1) >= width) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            output.lanes = lanes(0);
            output.lanes[number(1)] = scalar(inputs[2]);
            break;
        case SIMDIntrinsic::Lookup:
            if (!floating) {
                for (auto i = 0uz; i < width; ++i) {
                    const auto index = lanes(1)[i];
                    output.lanes[i] = index < width ? lanes(0)[index] : 0;
                }
            } else {
                std::unreachable();
            }
            break;
        case SIMDIntrinsic::Extract:
            if (number(2) > width) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            for (auto i = 0uz; i < width; ++i) {
                const auto index = number(2) + i;
                output.lanes[i] = index < width ? lanes(0)[index] : lanes(1)[index - width];
            }
            break;
        case SIMDIntrinsic::ShiftLeft:
        case SIMDIntrinsic::ShiftRight:
            if (number(1) >= 8) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            if (!floating) {
                for (auto i = 0uz; i < width; ++i) {
                    output.lanes[i] = static_cast<std::uint8_t>(
                        intrinsic == SIMDIntrinsic::ShiftLeft ? lanes(0)[i] << number(1)
                                                              : lanes(0)[i] >> number(1)
                    );
                }
            } else {
                std::unreachable();
            }
            break;
        case SIMDIntrinsic::Sum: {
            auto sum = 0u;
            for (const auto lane : lanes(0)) {
                sum += lane;
            }
            return ConstantFact {.type = result, .value = IntegerConstant::from_parts(sum, false)};
        }
        case SIMDIntrinsic::FirstOr: {
            auto first = number(1);
            for (auto i = 0uz; i < width; ++i) {
                if (mask(0)[i]) {
                    first = i;
                    break;
                }
            }
            return ConstantFact {
                .type = result,
                .value = IntegerConstant::from_parts(first, false)
            };
        }
        case SIMDIntrinsic::Select:
            for (auto i = 0uz; i < width; ++i) {
                output.lanes[i] = mask(0)[i] ? lanes(1)[i] : lanes(2)[i];
            }
            break;
        case SIMDIntrinsic::Bits:
        case SIMDIntrinsic::Count:
        case SIMDIntrinsic::Any:
        case SIMDIntrinsic::All:   {
            auto bits = 0ull;
            for (auto i = 0uz; i < width; ++i) {
                if (mask(0)[i]) {
                    bits |= 1ull << i;
                }
            }
            if (intrinsic == SIMDIntrinsic::Any || intrinsic == SIMDIntrinsic::All) {
                return ConstantFact {
                    .type = result,
                    .value = BooleanConstant {
                        .value = intrinsic == SIMDIntrinsic::Any ? bits != 0
                                                                 : bits == ((1ull << width) - 1ull)
                    }
                };
            }
            return ConstantFact {
                .type = result,
                .value = IntegerConstant::from_parts(
                    intrinsic == SIMDIntrinsic::Bits ? bits : std::popcount(bits),
                    false
                )
            };
        }
        case SIMDIntrinsic::Load:
        case SIMDIntrinsic::LoadPartial:
        case SIMDIntrinsic::FromArray:
        case SIMDIntrinsic::ToArray:
            return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    return ConstantFact {.type = result, .value = output};
}

auto fold_simd_constant(
    const ExecutionValueAccess& values,
    SIMDIntrinsic intrinsic,
    BuiltinType owner,
    std::span<const std::optional<ConstantID>> operands,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    auto inputs = std::vector<ConstantFact>();
    for (const auto operand : operands) {
        const auto fact = load_constant_fact(values, operand);
        if (!fact) {
            return std::unexpected(fact.error());
        }
        inputs.push_back(**fact);
    }
    return evaluate_simd_constant_value(values, intrinsic, owner, inputs, result);
}
