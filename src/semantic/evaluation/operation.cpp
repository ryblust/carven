module carven:semantic.evaluation.operation.impl;

import :diagnostics.code;
import :semantic.evaluation.operation;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.operation;
import :semantic.semir.simd;
import :semantic.semir.slice;
import :semantic.semir.type;
import :support.invariant;
import std;

namespace {

template<typename Number>
auto parsed_number(std::string_view text) noexcept -> std::optional<Number> {
    if (text.empty()) {
        return std::nullopt;
    }
    auto value = Number {};
    const auto result = [&]() noexcept {
        if constexpr (std::is_floating_point_v<Number>) {
            return std::from_chars(
                text.data(),
                text.data() + text.size(),
                value,
                std::chars_format::general
            );
        } else {
            return std::from_chars(text.data(), text.data() + text.size(), value);
        }
    }();
    if (result.ec != std::errc {} || result.ptr != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

auto parse_number_constant(
    const ExecutionValueAccess& values,
    BuiltinType number,
    std::string_view text
) noexcept -> std::optional<ConstantAtom> {
    const auto integer = [&]<typename Number>() noexcept -> std::optional<ConstantAtom> {
        const auto parsed = parsed_number<Number>(text);
        if (!parsed) {
            return std::nullopt;
        }
        using Unsigned = std::make_unsigned_t<Number>;
        const auto negative = std::is_signed_v<Number> && *parsed < 0;
        const auto bits = static_cast<Unsigned>(*parsed);
        const auto magnitude = negative ? static_cast<Unsigned>(Unsigned {0} - bits) : bits;
        return ConstantAtom {
            .type = values.builtin_type(number),
            .value = IntegerConstant::from_parts(magnitude, negative)
        };
    };
    const auto floating =
        [&]<typename Number, typename Constant>() noexcept -> std::optional<ConstantAtom> {
        const auto parsed = parsed_number<Number>(text);
        if (!parsed) {
            return std::nullopt;
        }
        return ConstantAtom {
            .type = values.builtin_type(number),
            .value = Constant {.value = *parsed}
        };
    };
    switch (number) {
        case BuiltinType::I8:    return integer.operator()<std::int8_t>();
        case BuiltinType::I16:   return integer.operator()<std::int16_t>();
        case BuiltinType::I32:   return integer.operator()<std::int32_t>();
        case BuiltinType::I64:   return integer.operator()<std::int64_t>();
        case BuiltinType::Isize: return integer.operator()<std::intptr_t>();
        case BuiltinType::U8:    return integer.operator()<std::uint8_t>();
        case BuiltinType::U16:   return integer.operator()<std::uint16_t>();
        case BuiltinType::U32:   return integer.operator()<std::uint32_t>();
        case BuiltinType::U64:   return integer.operator()<std::uint64_t>();
        case BuiltinType::Usize: return integer.operator()<std::uintptr_t>();
        case BuiltinType::F32:   return floating.operator()<float, F32Constant>();
        case BuiltinType::F64:   return floating.operator()<double, F64Constant>();
        default: invariant_violation("number parsing requires a scalar numeric type");
    }
}

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
        case ConstantEvaluationFailure::InvalidClampBounds:
            return ConstantEvaluationDiagnostic {
                .message = "clamp requires lower <= upper",
                .code = DiagnosticCode::AssertionFailed,
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
        if constexpr (std::same_as<Item, TextConstant> || std::same_as<Item, CStringConstant>) {
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
                             || std::same_as<Item, TupleConstant>
                             || std::same_as<Item, SliceConstant>
                             || std::same_as<Item, VectorConstant>) {
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
                    || std::same_as<Item, CharacterConstant>
                    || std::same_as<Item, ByteSequenceConstant>
                    || std::same_as<Item, VoidConstant>,
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

auto constant_integer(TypeID type, IntegerConstant value) noexcept -> ConstantAtom {
    return {.type = type, .value = value};
}

auto constant_boolean(const ExecutionValueAccess& values, TypeID type, bool value) noexcept
    -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
    if (!is_builtin(values, type, BuiltinType::Bool)) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    return ConstantAtom {.type = type, .value = BooleanConstant {.value = value}};
}

auto finish_constant_integer(
    const ExecutionValueAccess& values,
    TypeID result,
    IntegerConstant value,
    BuiltinType type,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
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
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
    const auto finish =
        [&](auto value) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
        if (result != operand_type) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return ConstantAtom {.type = result, .value = Floating {.value = value}};
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
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
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
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
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

namespace {

template<typename Payload>
auto equal_constant_payload(
    const ConstantValueReader& values,
    const Payload& left,
    const Payload& right
) noexcept -> bool {
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
        } else if constexpr (std::same_as<Value, StructConstant>
                             || std::same_as<Value, VectorConstant>
                             || std::same_as<Value, ByteSequenceConstant>) {
            invariant_violation("structure constants do not support language equality");
        } else if constexpr (std::same_as<Value, ArrayConstant>
                             || std::same_as<Value, TupleConstant>) {
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

} // namespace

auto constant_value_equal(
    const ConstantValueReader& values,
    const ConstantValue& left,
    const ConstantValue& right
) noexcept -> bool {
    return equal_constant_payload(values, left, right);
}

auto constant_value_equal(
    const ConstantValueReader& values,
    const ConstantAtomValue& left,
    const ConstantAtomValue& right
) noexcept -> bool {
    return equal_constant_payload(values, left, right);
}

auto evaluate_unary_constant_value(
    const ExecutionValueAccess& values,
    UnaryOperator operation,
    const ConstantAtom& operand,
    TypeID result,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
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
        const auto element = floating ? BuiltinType::F32 : BuiltinType::U8;
        for (auto i = 0uz; i < layout->width; ++i) {
            const auto lane = output.lane(i, element);
            output.set_lane(i, element, floating ? lane ^ 0x80000000u : (~lane & 255u));
        }
        return ConstantAtom {.type = result, .value = output};
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
                return ConstantAtom {
                    .type = result,
                    .value = F32Constant {.value = -floating->value}
                };
            }
            if (const auto* floating = std::get_if<F64Constant>(&operand.value)) {
                if (result != operand.type) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                return ConstantAtom {
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
                            *type,
                            IntegerArithmetic::Checked
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
                    *type,
                    IntegerArithmetic::Checked
                );
            }
            break;
    }
    return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
}

auto evaluate_binary_constant_value(
    const ExecutionValueAccess& values,
    BinaryOperator operation,
    const ConstantAtom& left,
    const ConstantAtom& right,
    TypeID result,
    IntegerArithmetic arithmetic
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
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
        auto output = SIMDConstant {};
        const auto floating = *owner == layout->vector && layout->element == BuiltinType::F32;
        const auto element = floating ? BuiltinType::F32 : BuiltinType::U8;
        const auto output_element = comparison ? BuiltinType::U8 : element;
        for (auto i = 0uz; i < layout->width; ++i) {
            const auto a = first->lane(i, element);
            const auto b = second->lane(i, element);
            if (floating) {
                const auto x = std::bit_cast<float>(a);
                const auto y = std::bit_cast<float>(b);
                auto predicate = false;
                switch (operation) {
                    case BinaryOperator::Add:
                        output.set_lane(i, output_element, std::bit_cast<std::uint32_t>(x + y));
                        break;
                    case BinaryOperator::Subtract:
                        output.set_lane(i, output_element, std::bit_cast<std::uint32_t>(x - y));
                        break;
                    case BinaryOperator::Multiply:
                        output.set_lane(i, output_element, std::bit_cast<std::uint32_t>(x * y));
                        break;
                    case BinaryOperator::Divide:
                        output.set_lane(i, output_element, std::bit_cast<std::uint32_t>(x / y));
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
                    output.set_lane(i, output_element, predicate ? 255u : 0u);
                }
            } else {
                switch (operation) {
                    case BinaryOperator::Add:
                        output.set_lane(i, output_element, (a + b) & 255u);
                        break;
                    case BinaryOperator::Subtract:
                        output.set_lane(i, output_element, (a - b) & 255u);
                        break;
                    case BinaryOperator::BitwiseAnd:
                        output.set_lane(i, output_element, a & b);
                        break;
                    case BinaryOperator::BitwiseOr:
                        output.set_lane(i, output_element, a | b);
                        break;
                    case BinaryOperator::BitwiseXor:
                        output.set_lane(i, output_element, a ^ b);
                        break;
                    case BinaryOperator::Equal:
                        output.set_lane(i, output_element, a == b ? 255u : 0u);
                        break;
                    case BinaryOperator::NotEqual:
                        output.set_lane(i, output_element, a != b ? 255u : 0u);
                        break;
                    case BinaryOperator::Less:
                        output.set_lane(i, output_element, a < b ? 255u : 0u);
                        break;
                    case BinaryOperator::LessEqual:
                        output.set_lane(i, output_element, a <= b ? 255u : 0u);
                        break;
                    case BinaryOperator::Greater:
                        output.set_lane(i, output_element, a > b ? 255u : 0u);
                        break;
                    case BinaryOperator::GreaterEqual:
                        output.set_lane(i, output_element, a >= b ? 255u : 0u);
                        break;
                    default: return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
            }
        }
        return ConstantAtom {.type = result, .value = output};
    }
    if (operation == BinaryOperator::Equal || operation == BinaryOperator::NotEqual) {
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
            constexpr auto bits = [](IntegerConstant value) static noexcept {
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
    const ConstantAtom& operand,
    TypeID result
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
    const auto target = builtin_type(values, result);
    if (kind == CastKind::PointerRead
        && std::holds_alternative<NullPointerConstant>(operand.value)) {
        if (constant_pointer_narrows(values, operand.type, result)) {
            return ConstantAtom {.type = result, .value = NullPointerConstant {}};
        }
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    if ((kind == CastKind::PointerErase || kind == CastKind::PointerRestore)
        && std::holds_alternative<NullPointerConstant>(operand.value)) {
        return ConstantAtom {.type = result, .value = NullPointerConstant {}};
    }
    if (kind == CastKind::Identity) {
        if (operand.type != result) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return ConstantAtom {.type = result, .value = operand.value};
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
            const auto convert = [&]<typename Floating>() noexcept -> ConstantAtom {
                using Native = decltype(Floating::value);
                const auto value = integer->negative() ? static_cast<Native>(*integer->as_signed())
                                                       : static_cast<Native>(integer->magnitude());
                return ConstantAtom {.type = result, .value = Floating {.value = value}};
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
            return ConstantAtom {
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
    const ConstantAtom& operand,
    TypeID result
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
    const auto* string = std::get_if<TextConstant>(&operand.value);
    if (string == nullptr || !is_builtin(values, operand.type, BuiltinType::Str)) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    const auto bytes = values.spelling(string->value);
    switch (intrinsic) {
        case TextIntrinsic::ByteLength: {
            const auto target = builtin_type(values, result);
            if (!target.has_value() || *target != BuiltinType::Usize) {
                return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
            }
            return finish_constant_integer(
                values,
                result,
                IntegerConstant::from_parts(static_cast<std::uint64_t>(bytes.size()), false),
                *target,
                IntegerArithmetic::Checked
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
    const auto* array = std::get_if<ArrayConstant>(&operand.value);
    const auto* slice = std::get_if<SliceConstant>(&operand.value);
    const auto* bytes = std::get_if<ByteSequenceConstant>(&operand.value);
    const auto contract = slice_intrinsic_contract(intrinsic);
    if ((!array && !slice && !bytes)
        || (bytes
            && !std::holds_alternative<SliceTypeValue>(values.type_copy(operand.type).value))) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    if (!slice_intrinsic_accepts_receiver(
            contract.receiver,
            array ? SliceIntrinsicShape::Array : SliceIntrinsicShape::Slice
        )
        || bounds.size() != contract.arguments.size()) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    if (intrinsic == SliceIntrinsic::View) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    const auto size = bytes ? bytes->bytes.size()
        : array             ? array->elements.size()
                            : slice->elements.size();
    if (intrinsic == SliceIntrinsic::IsEmpty) {
        return constant_boolean(values, result, size == 0uz).transform(constant_fact);
    }
    if (intrinsic == SliceIntrinsic::Length) {
        if (!is_builtin(values, result, BuiltinType::Usize)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return ConstantFact {
            .type = result,
            .value = IntegerConstant::from_parts(size, false),
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
    if (start > end || end > size) {
        return std::unexpected(ConstantEvaluationFailure::SliceOutOfBounds);
    }
    if (bytes) {
        const auto selected = std::span(bytes->bytes).subspan(start, end - start);
        return ConstantFact {
            .type = result,
            .value = ByteSequenceConstant {
                .bytes = std::vector(selected.begin(), selected.end()),
                .capacity = selected.size(),
            },
        };
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
    const auto atom = constant_atom(**fact);
    if (!atom) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    return evaluate_unary_constant_value(
               values,
               operation,
               *atom,
               result,
               IntegerArithmetic::Wrapping
    )
        .transform(constant_fact);
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
    if ((operation == BinaryOperator::Equal || operation == BinaryOperator::NotEqual)
        && !std::holds_alternative<SIMDConstant>((*left_fact)->value)) {
        if (std::holds_alternative<SliceConstant>((*left_fact)->value)
            || std::holds_alternative<ByteSequenceConstant>((*left_fact)->value)) {
            return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
        }
        const auto equal = constant_value_equal(values, (*left_fact)->value, (*right_fact)->value);
        return constant_boolean(values, result, operation == BinaryOperator::Equal ? equal : !equal)
            .transform(constant_fact);
    }
    const auto lhs = constant_atom(**left_fact);
    const auto rhs = constant_atom(**right_fact);
    if (!lhs || !rhs) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    return evaluate_binary_constant_value(
               values,
               operation,
               *lhs,
               *rhs,
               result,
               IntegerArithmetic::Wrapping
    )
        .transform(constant_fact);
}

auto fold_cast_constant(
    const ExecutionValueAccess& values,
    CastKind kind,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    if (kind == CastKind::IntegerToFloating) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    const auto fact = load_constant_fact(values, operand);
    if (!fact.has_value()) {
        return std::unexpected(fact.error());
    }
    if (kind == CastKind::Identity) {
        if ((*fact)->type != result) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return **fact;
    }
    const auto atom = constant_atom(**fact);
    if (!atom) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    return evaluate_cast_constant_value(values, kind, *atom, result).transform(constant_fact);
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
    const auto atom = constant_atom(**fact);
    if (!atom) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    return evaluate_text_intrinsic_constant_value(values, intrinsic, *atom, result)
        .transform(constant_fact);
}

auto evaluate_simd_constant_value(
    const ExecutionValueAccess& values,
    SIMDIntrinsic intrinsic,
    BuiltinType owner,
    std::span<const ConstantAtom> inputs,
    TypeID result
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
    auto indices = std::optional<BuiltinType>();
    if (intrinsic == SIMDIntrinsic::Lookup) {
        const auto type = values.type_copy(result);
        const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
        if (!builtin
            || (builtin->kind != BuiltinType::U8x16 && builtin->kind != BuiltinType::U8x32)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        indices = builtin->kind;
    }
    const auto layout = *simd_layout(owner);
    const auto width = layout.width;
    const auto floating = layout.element == BuiltinType::F32;
    using Vector = SIMDConstant;
    using Mask = SIMDConstant;
    const auto scalar = [&](const ConstantAtom& fact) noexcept -> std::uint32_t {
        return floating
            ? std::bit_cast<std::uint32_t>(std::get<F32Constant>(fact.value).value)
            : static_cast<std::uint32_t>(std::get<IntegerConstant>(fact.value).magnitude());
    };
    const auto scalar_fact = [&](std::uint32_t bits) noexcept -> ConstantAtom {
        if (floating) {
            return {.type = result, .value = F32Constant {.value = std::bit_cast<float>(bits)}};
        }
        return {.type = result, .value = IntegerConstant::from_parts(bits, false)};
    };
    const auto number = [&](std::size_t index) noexcept {
        return std::get<IntegerConstant>(inputs[index].value).magnitude();
    };
    const auto lane = [&](std::size_t input, std::size_t index) noexcept {
        return std::get<Vector>(inputs[input].value).lane(index, layout.element);
    };
    const auto mask = [&](std::size_t input, std::size_t index) noexcept {
        return std::get<Mask>(inputs[input].value).lane(index, BuiltinType::U8);
    };
    auto output = Vector {};
    switch (intrinsic) {
        case SIMDIntrinsic::Splat:
            for (auto i = 0uz; i < width; ++i) {
                output.set_lane(i, layout.element, scalar(inputs[0]));
            }
            break;
        case SIMDIntrinsic::FromBits:
        case SIMDIntrinsic::Prefix:   {
            auto output_mask = Mask {};
            if (intrinsic == SIMDIntrinsic::Prefix && number(0) > width) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            for (auto i = 0uz; i < width; ++i) {
                const auto active = intrinsic == SIMDIntrinsic::Prefix
                    ? i < number(0)
                    : (number(0) & (1ull << i)) != 0;
                output_mask.set_lane(i, BuiltinType::U8, active ? 255u : 0u);
            }
            return ConstantAtom {.type = result, .value = output_mask};
        }
        case SIMDIntrinsic::Lane:
            if (number(1) >= width) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            return scalar_fact(lane(0, number(1)));
        case SIMDIntrinsic::WithLane:
            if (number(1) >= width) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            output = std::get<Vector>(inputs[0].value);
            output.set_lane(number(1), layout.element, scalar(inputs[2]));
            break;
        case SIMDIntrinsic::Lookup:
            if (!floating) {
                for (auto i = 0uz; i < simd_layout(*indices)->width; ++i) {
                    const auto index = lane(1, i);
                    output.set_lane(i, layout.element, index < width ? lane(0, index) : 0);
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
                output.set_lane(
                    i,
                    layout.element,
                    index < width ? lane(0, index) : lane(1, index - width)
                );
            }
            break;
        case SIMDIntrinsic::ShiftLeft:
        case SIMDIntrinsic::ShiftRight:
            if (number(1) >= 8) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            if (!floating) {
                for (auto i = 0uz; i < width; ++i) {
                    output.set_lane(
                        i,
                        layout.element,
                        static_cast<std::uint8_t>(
                            intrinsic == SIMDIntrinsic::ShiftLeft ? lane(0, i) << number(1)
                                                                  : lane(0, i) >> number(1)
                        )
                    );
                }
            } else {
                std::unreachable();
            }
            break;
        case SIMDIntrinsic::Sum: {
            auto sum = 0u;
            for (auto i = 0uz; i < width; ++i) {
                sum += lane(0, i);
            }
            return ConstantAtom {.type = result, .value = IntegerConstant::from_parts(sum, false)};
        }
        case SIMDIntrinsic::FirstOr: {
            auto first = number(1);
            for (auto i = 0uz; i < width; ++i) {
                if (mask(0, i)) {
                    first = i;
                    break;
                }
            }
            return ConstantAtom {
                .type = result,
                .value = IntegerConstant::from_parts(first, false)
            };
        }
        case SIMDIntrinsic::Select:
            for (auto i = 0uz; i < width; ++i) {
                output.set_lane(i, layout.element, mask(0, i) ? lane(1, i) : lane(2, i));
            }
            break;
        case SIMDIntrinsic::Bits:
        case SIMDIntrinsic::Count:
        case SIMDIntrinsic::Any:
        case SIMDIntrinsic::All:   {
            auto bits = 0ull;
            for (auto i = 0uz; i < width; ++i) {
                if (mask(0, i)) {
                    bits |= 1ull << i;
                }
            }
            if (intrinsic == SIMDIntrinsic::Any || intrinsic == SIMDIntrinsic::All) {
                return ConstantAtom {
                    .type = result,
                    .value = BooleanConstant {
                        .value = intrinsic == SIMDIntrinsic::Any ? bits != 0
                                                                 : bits == ((1ull << width) - 1ull)
                    }
                };
            }
            return ConstantAtom {
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
        case SIMDIntrinsic::Swizzle:
        case SIMDIntrinsic::ToArray:
            return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    return ConstantAtom {.type = result, .value = output};
}

auto fold_simd_constant(
    const ExecutionValueAccess& values,
    SIMDIntrinsic intrinsic,
    BuiltinType owner,
    std::span<const std::optional<ConstantID>> operands,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    if (intrinsic == SIMDIntrinsic::Swizzle) {
        const auto input = load_constant_fact(values, operands[0]);
        const auto controls = load_constant_fact(values, operands[1]);
        if (!input || !controls) {
            return std::unexpected(!input ? input.error() : controls.error());
        }
        const auto layout = *simd_layout(owner);
        const auto& vector = std::get<SIMDConstant>((*input)->value);
        const auto& indices = std::get<ArrayConstant>((*controls)->value).elements;
        auto output = SIMDConstant {};
        for (auto index = 0uz; index < layout.width; ++index) {
            const auto& control = std::get<IntegerConstant>(values.constant(indices[index]).value);
            if (control.negative() || control.magnitude() >= layout.width) {
                return std::unexpected(ConstantEvaluationFailure::SIMDOutOfBounds);
            }
            output
                .set_lane(index, layout.element, vector.lane(control.magnitude(), layout.element));
        }
        return ConstantFact {.type = result, .value = output};
    }
    auto inputs = std::vector<ConstantAtom>();
    for (const auto operand : operands) {
        const auto fact = load_constant_fact(values, operand);
        if (!fact) {
            return std::unexpected(fact.error());
        }
        const auto atom = constant_atom(**fact);
        if (!atom) {
            return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
        }
        inputs.push_back(*atom);
    }
    return evaluate_simd_constant_value(values, intrinsic, owner, inputs, result)
        .transform(constant_fact);
}

auto evaluate_float_intrinsic_constant_value(
    const ExecutionValueAccess& values,
    FloatIntrinsic intrinsic,
    std::span<const ConstantAtom> operands,
    TypeID result
) noexcept -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
    if (operands.size() != float_intrinsic_arguments(intrinsic) + 1) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto receiver_type = operands.front().type;
    for (const auto& operand : operands) {
        if (operand.type != receiver_type) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
    }
    const auto owner = builtin_type(values, receiver_type);
    const auto layout = owner ? simd_layout(*owner) : std::nullopt;
    if (layout && (*owner != layout->vector || layout->element != BuiltinType::F32)) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    if (result
        != (intrinsic == FloatIntrinsic::IsFinite
                ? values.builtin_type(layout ? layout->mask : BuiltinType::Bool)
                : receiver_type)) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto evaluate = [&]<typename Floating>(std::span<const Floating> inputs) noexcept
        -> std::expected<std::variant<Floating, BooleanConstant>, ConstantEvaluationFailure> {
        const auto value = inputs.front().value;
        const auto finish = [](auto output) static noexcept {
            return Floating {.value = output};
        };
        switch (intrinsic) {
            case FloatIntrinsic::IsFinite: return BooleanConstant {.value = std::isfinite(value)};
            case FloatIntrinsic::Sqrt:     return finish(std::sqrt(value));
            case FloatIntrinsic::Abs:      return finish(std::fabs(value));
            case FloatIntrinsic::Min:
            case FloatIntrinsic::Max:      {
                const auto second = inputs[1].value;
                if (value != value) {
                    return finish(value);
                }
                if (second != second) {
                    return finish(second);
                }
                return finish(
                    (intrinsic == FloatIntrinsic::Min ? second < value : second > value) ? second
                                                                                         : value
                );
            }
            case FloatIntrinsic::Clamp: {
                const auto lower = inputs[1].value;
                const auto upper = inputs[2].value;
                if (!(lower <= upper)) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidClampBounds);
                }
                return finish(value < lower ? lower : value > upper ? upper : value);
            }
        }
        std::unreachable();
    };
    const auto scalar = [&]<typename Floating>() noexcept
        -> std::expected<ConstantAtom, ConstantEvaluationFailure> {
        auto inputs = std::array<Floating, 3> {};
        for (auto i = 0uz; i < operands.size(); ++i) {
            inputs[i] = std::get<Floating>(operands[i].value);
        }
        return evaluate(std::span<const Floating>(inputs).first(operands.size()))
            .transform([&](const auto& output) noexcept {
                return output.visit([&](const auto& value) noexcept {
                    return ConstantAtom {.type = result, .value = value};
                });
            });
    };
    if (std::holds_alternative<F32Constant>(operands.front().value)) {
        return scalar.template operator()<F32Constant>();
    }
    if (std::holds_alternative<F64Constant>(operands.front().value)) {
        return scalar.template operator()<F64Constant>();
    }
    if (layout) {
        const auto predicate = intrinsic == FloatIntrinsic::IsFinite;
        auto output = SIMDConstant {};
        auto lane_operands = std::array<F32Constant, 3> {};
        for (auto lane = 0uz; lane < layout->width; ++lane) {
            for (auto i = 0uz; i < operands.size(); ++i) {
                lane_operands[i].value = std::bit_cast<float>(
                    std::get<SIMDConstant>(operands[i].value).lane(lane, BuiltinType::F32)
                );
            }
            auto evaluated =
                evaluate(std::span<const F32Constant>(lane_operands).first(operands.size()));
            if (!evaluated) {
                return std::unexpected(evaluated.error());
            }
            output.set_lane(
                lane,
                predicate ? BuiltinType::U8 : BuiltinType::F32,
                predicate ? (std::get<BooleanConstant>(*evaluated).value ? 255u : 0u)
                          : std::bit_cast<std::uint32_t>(std::get<F32Constant>(*evaluated).value)
            );
        }
        return ConstantAtom {.type = result, .value = output};
    }
    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
}
