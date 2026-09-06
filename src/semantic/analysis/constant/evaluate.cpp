module carven:semantic.analysis.constant.evaluate.impl;

import :diagnostics.code;
import :frontend.literal;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :support.invariant;
import std;

auto constant_evaluation_diagnostic(ConstantEvaluationFailure failure) noexcept
    -> std::optional<ConstantEvaluationDiagnostic> {
    switch (failure) {
        case ConstantEvaluationFailure::IntegerOverflow:
            return ConstantEvaluationDiagnostic {
                .message = "integer constant overflow",
                .code = DiagnosticCode::ConstOverflow,
            };
        case ConstantEvaluationFailure::DivideByZero:
            return ConstantEvaluationDiagnostic {
                .message = "division by zero in constant expression",
                .code = DiagnosticCode::ConstDivideByZero,
            };
        case ConstantEvaluationFailure::ShiftOutOfRange:
            return ConstantEvaluationDiagnostic {
                .message = "shift count is outside the integer type width",
                .code = DiagnosticCode::ConstShiftRange,
            };
        case ConstantEvaluationFailure::IntegerLiteralOutOfRange:
            return ConstantEvaluationDiagnostic {
                .message = "integer constant is out of range",
                .code = DiagnosticCode::ConstOverflow,
            };
        case ConstantEvaluationFailure::FloatingLiteralOutOfRange:
            return ConstantEvaluationDiagnostic {
                .message = "floating constant is out of range",
                .code = DiagnosticCode::ConstOverflow,
            };
        case ConstantEvaluationFailure::IntegerLiteralNotRepresentable:
            return ConstantEvaluationDiagnostic {
                .message = "integer literal is not representable in its type",
                .code = DiagnosticCode::ConstLiteralRange,
            };
        case ConstantEvaluationFailure::OperandNotConstant:
        case ConstantEvaluationFailure::UnsupportedOperation:
        case ConstantEvaluationFailure::InvalidOperation:     return std::nullopt;
    }
    std::unreachable();
}

namespace {

auto builtin_type(const ProgramDraft& draft, TypeID type) noexcept -> std::optional<BuiltinType> {
    const auto canonical = draft.type_copy(type);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    return builtin == nullptr ? std::nullopt : std::optional(builtin->kind);
}

auto validate_constant_value(const ProgramDraft& draft, const ConstantValue& value) noexcept
    -> void {
    std::visit(
        [&](const auto& item) noexcept {
            using Item = std::remove_cvref_t<decltype(item)>;
            if constexpr (std::same_as<Item, StringConstant>) {
                if (!draft.owns(item.value)) {
                    invariant_violation("constant evaluator received a foreign spelling");
                }
            } else if constexpr (std::same_as<Item, NumericEnumConstant>) {
                if (item.enum_case.owner() != draft.identity()) {
                    invariant_violation("constant evaluator received a foreign enum case");
                }
            } else if constexpr (std::same_as<Item, PayloadEnumConstant>) {
                if (item.enum_case.owner() != draft.identity()) {
                    invariant_violation("constant evaluator received a foreign enum case");
                }
                for (const auto child : item.payload) {
                    static_cast<void>(draft.constant_copy(child));
                }
            } else {
                static_assert(
                    std::same_as<Item, IntegerConstant>
                        || std::same_as<Item, BooleanConstant>
                        || std::same_as<Item, F32Constant>
                        || std::same_as<Item, F64Constant>
                        || std::same_as<Item, CharacterConstant>,
                    "unhandled constant value validation"
                );
            }
        },
        value
    );
}

auto validate_constant_fact(const ProgramDraft& draft, const ConstantFact& fact) noexcept -> void {
    const auto type = draft.type_copy(fact.type);
    validate_constant_value(draft, fact.value);
    const auto matches = std::visit(
        [&](const auto& value) noexcept -> bool {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, IntegerConstant>) {
                const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
                return builtin != nullptr
                    && builtin_is_integer(builtin->kind)
                    && integer_constant_fits(value, builtin->kind);
            } else if constexpr (std::same_as<Value, BooleanConstant>) {
                const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
                return builtin != nullptr && builtin->kind == BuiltinType::Bool;
            } else if constexpr (std::same_as<Value, StringConstant>) {
                const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
                return builtin != nullptr && builtin->kind == BuiltinType::Str;
            } else if constexpr (std::same_as<Value, F32Constant>) {
                const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
                return builtin != nullptr && builtin->kind == BuiltinType::F32;
            } else if constexpr (std::same_as<Value, F64Constant>) {
                const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
                return builtin != nullptr && builtin->kind == BuiltinType::F64;
            } else if constexpr (std::same_as<Value, CharacterConstant>) {
                const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
                return builtin != nullptr && builtin->kind == BuiltinType::Char;
            } else if constexpr (std::same_as<Value, NumericEnumConstant>
                                 || std::same_as<Value, PayloadEnumConstant>) {
                return std::holds_alternative<EnumTypeValue>(type.value);
            } else {
                static_assert(std::same_as<Value, void>, "new constant requires type validation");
            }
        },
        fact.value
    );
    if (!matches) {
        invariant_violation("constant evaluator received a value with a mismatched type");
    }
}

auto is_builtin(const ProgramDraft& draft, TypeID type, BuiltinType expected) noexcept -> bool {
    return builtin_type(draft, type) == expected;
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

auto constant_boolean(const ProgramDraft& draft, TypeID type, bool value) noexcept
    -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    if (!is_builtin(draft, type, BuiltinType::Bool)) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    return ConstantFact {.type = type, .value = BooleanConstant {.value = value}};
}

auto finish_constant_integer(
    const ProgramDraft& draft,
    TypeID result,
    IntegerConstant value,
    BuiltinType type
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    if (builtin_type(draft, result) != type) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    if (!integer_constant_fits(value, type)) {
        return std::unexpected(ConstantEvaluationFailure::IntegerOverflow);
    }
    return constant_integer(result, value);
}

auto evaluate_signed_integer(
    const ProgramDraft& draft,
    BinaryOperator operation,
    IntegerConstant left,
    IntegerConstant right,
    BuiltinType type,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto width = builtin_integer_width(type);
    const auto lhs = left.as_signed();
    const auto rhs = right.as_signed();
    if (!width.has_value() || !lhs.has_value() || !rhs.has_value()) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto finish = [&](IntegerConstant value) noexcept {
        return finish_constant_integer(draft, result, value, type);
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
        case BinaryOperator::Less:         return constant_boolean(draft, result, *lhs < *rhs);
        case BinaryOperator::LessEqual:    return constant_boolean(draft, result, *lhs <= *rhs);
        case BinaryOperator::Greater:      return constant_boolean(draft, result, *lhs > *rhs);
        case BinaryOperator::GreaterEqual: return constant_boolean(draft, result, *lhs >= *rhs);
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual:
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    std::unreachable();
}

auto evaluate_unsigned_integer(
    const ProgramDraft& draft,
    BinaryOperator operation,
    IntegerConstant left,
    IntegerConstant right,
    BuiltinType type,
    TypeID result
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
        return finish_constant_integer(draft, result, value, type);
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
        case BinaryOperator::Less:         return constant_boolean(draft, result, *lhs < *rhs);
        case BinaryOperator::LessEqual:    return constant_boolean(draft, result, *lhs <= *rhs);
        case BinaryOperator::Greater:      return constant_boolean(draft, result, *lhs > *rhs);
        case BinaryOperator::GreaterEqual: return constant_boolean(draft, result, *lhs >= *rhs);
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual:
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    std::unreachable();
}

auto suffix_builtin(NumericSuffix suffix) noexcept -> std::optional<BuiltinType> {
    switch (suffix) {
        case NumericSuffix::None:  return std::nullopt;
        case NumericSuffix::I8:    return BuiltinType::I8;
        case NumericSuffix::I16:   return BuiltinType::I16;
        case NumericSuffix::I32:   return BuiltinType::I32;
        case NumericSuffix::I64:   return BuiltinType::I64;
        case NumericSuffix::U8:    return BuiltinType::U8;
        case NumericSuffix::U16:   return BuiltinType::U16;
        case NumericSuffix::U32:   return BuiltinType::U32;
        case NumericSuffix::U64:   return BuiltinType::U64;
        case NumericSuffix::Isize: return BuiltinType::Isize;
        case NumericSuffix::Usize: return BuiltinType::Usize;
        case NumericSuffix::F32:   return BuiltinType::F32;
        case NumericSuffix::F64:   return BuiltinType::F64;
    }
    std::unreachable();
}

auto validate_expected_type(
    const ProgramDraft& draft,
    std::optional<ConstructionTypeRef> expected
) noexcept -> void {
    if (!expected.has_value()) {
        return;
    }
    std::visit(
        [&](const auto id) noexcept {
            using ID = std::remove_cvref_t<decltype(id)>;
            if constexpr (std::same_as<ID, TypeID>) {
                static_cast<void>(draft.type_copy(id));
            } else if constexpr (std::same_as<ID, TypeTermID>) {
                static_cast<void>(draft.construction_type_copy(id));
            } else {
                static_assert(std::same_as<ID, void>, "unhandled expected type reference");
            }
        },
        *expected
    );
}

auto normalized_numeric_type(
    ProgramDraft& draft,
    NumericSuffix suffix,
    bool floating,
    std::optional<ConstructionTypeRef> expected
) noexcept -> TypeID {
    if (const auto suffixed = suffix_builtin(suffix)) {
        return draft.intern_builtin_type(*suffixed);
    }
    if (expected.has_value()) {
        if (const auto* concrete = std::get_if<TypeID>(&*expected)) {
            const auto expected_builtin = builtin_type(draft, *concrete);
            if (expected_builtin.has_value()
                && builtin_is_numeric(*expected_builtin)
                && builtin_is_integer(*expected_builtin) == !floating) {
                return *concrete;
            }
        }
    }
    return draft.intern_builtin_type(floating ? BuiltinType::F64 : BuiltinType::I32);
}

} // namespace

auto load_constant_fact(const ProgramDraft& draft, std::optional<ConstantID> constant) noexcept
    -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    if (!constant.has_value()) {
        return std::unexpected(ConstantEvaluationFailure::OperandNotConstant);
    }
    auto fact = draft.constant_copy(*constant);
    validate_constant_fact(draft, fact);
    return fact;
}

auto constant_value_equal(
    const ProgramDraft& draft,
    const ConstantValue& left,
    const ConstantValue& right
) noexcept -> bool {
    validate_constant_value(draft, left);
    validate_constant_value(draft, right);
    if (left.index() != right.index()) {
        return false;
    }
    return std::visit(
        [&](const auto& left_value) noexcept -> bool {
            using Value = std::remove_cvref_t<decltype(left_value)>;
            const auto& right_value = std::get<Value>(right);
            if constexpr (std::same_as<Value, PayloadEnumConstant>) {
                if (left_value.enum_case != right_value.enum_case
                    || left_value.payload.size() != right_value.payload.size()) {
                    return false;
                }
                for (const auto [left_child, right_child] :
                     std::views::zip(left_value.payload, right_value.payload)) {
                    const auto left_fact = draft.constant_copy(left_child);
                    const auto right_fact = draft.constant_copy(right_child);
                    if (left_fact.type != right_fact.type
                        || !constant_value_equal(draft, left_fact.value, right_fact.value)) {
                        return false;
                    }
                }
                return true;
            } else if constexpr (std::same_as<Value, F32Constant>
                                 || std::same_as<Value, F64Constant>) {
                return left_value.value == right_value.value;
            } else {
                return left_value == right_value;
            }
        },
        left
    );
}

auto evaluate_unary_constant_value(
    const ProgramDraft& draft,
    UnaryOperator operation,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(draft, operand);
    static_cast<void>(draft.type_copy(result));
    switch (operation) {
        case UnaryOperator::LogicalNot:
            if (const auto* boolean = std::get_if<BooleanConstant>(&operand.value);
                boolean != nullptr && is_builtin(draft, operand.type, BuiltinType::Bool)) {
                return constant_boolean(draft, result, !boolean->value);
            }
            break;
        case UnaryOperator::Negate:
            if (const auto* integer = std::get_if<IntegerConstant>(&operand.value)) {
                const auto type = builtin_type(draft, operand.type);
                if (type.has_value() && builtin_is_integer(*type)) {
                    return finish_constant_integer(
                        draft,
                        result,
                        IntegerConstant::from_parts(integer->magnitude(), !integer->negative()),
                        *type
                    );
                }
            }
            break;
        case UnaryOperator::BitwiseNot:
            if (const auto* integer = std::get_if<IntegerConstant>(&operand.value)) {
                const auto type = builtin_type(draft, operand.type);
                const auto width = type.has_value() ? builtin_integer_width(*type) : std::nullopt;
                if (!type.has_value() || !width.has_value()) {
                    break;
                }
                if (builtin_is_signed_integer(*type)) {
                    const auto value = integer->as_signed();
                    if (value.has_value()) {
                        return finish_constant_integer(
                            draft,
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
                    draft,
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
    const ProgramDraft& draft,
    BinaryOperator operation,
    const ConstantFact& left,
    const ConstantFact& right,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(draft, left);
    validate_constant_fact(draft, right);
    static_cast<void>(draft.type_copy(result));
    if (left.type != right.type) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    if (operation == BinaryOperator::Equal || operation == BinaryOperator::NotEqual) {
        const auto equal = constant_value_equal(draft, left.value, right.value);
        return constant_boolean(draft, result, operation == BinaryOperator::Equal ? equal : !equal);
    }
    if (const auto* left_integer = std::get_if<IntegerConstant>(&left.value)) {
        const auto* right_integer = std::get_if<IntegerConstant>(&right.value);
        const auto type = builtin_type(draft, left.type);
        if (right_integer == nullptr || !type.has_value() || !builtin_is_integer(*type)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return builtin_is_signed_integer(*type) ? evaluate_signed_integer(
                                                      draft,
                                                      operation,
                                                      *left_integer,
                                                      *right_integer,
                                                      *type,
                                                      result
                                                  )
                                                : evaluate_unsigned_integer(
                                                      draft,
                                                      operation,
                                                      *left_integer,
                                                      *right_integer,
                                                      *type,
                                                      result
                                                  );
    }
    return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
}

auto evaluate_cast_constant_value(
    const ProgramDraft& draft,
    CastKind kind,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(draft, operand);
    const auto target = builtin_type(draft, result);
    if (kind == CastKind::Identity) {
        if (operand.type != result) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        return ConstantFact {.type = result, .value = operand.value};
    }
    if (!target.has_value()) {
        return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
    }
    const auto source = draft.type_copy(operand.type);
    const auto* source_builtin = std::get_if<BuiltinTypeValue>(&source.value);
    if (const auto* integer = std::get_if<IntegerConstant>(&operand.value)) {
        if (source_builtin == nullptr || !builtin_is_integer(source_builtin->kind)) {
            return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
        }
        if (kind == CastKind::IntegerToBool && *target == BuiltinType::Bool) {
            return constant_boolean(draft, result, integer->magnitude() != 0u);
        }
        if (kind == CastKind::IntegerToInteger && builtin_is_integer(*target)) {
            return constant_integer(result, normalize_integer_cast(*integer, *target));
        }
        if (kind == CastKind::IntegerToFloating
            && (*target == BuiltinType::F32 || *target == BuiltinType::F64)) {
            const auto value = integer->negative() ? -static_cast<double>(integer->magnitude())
                                                   : static_cast<double>(integer->magnitude());
            if (*target == BuiltinType::F32) {
                return ConstantFact {
                    .type = result,
                    .value = F32Constant {.value = static_cast<float>(value)},
                };
            }
            return ConstantFact {.type = result, .value = F64Constant {.value = value}};
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
    const ProgramDraft& draft,
    TextIntrinsic intrinsic,
    const ConstantFact& operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_constant_fact(draft, operand);
    const auto* string = std::get_if<StringConstant>(&operand.value);
    if (string == nullptr || !is_builtin(draft, operand.type, BuiltinType::Str)) {
        return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    const auto bytes = draft.spelling_copy(string->value);
    switch (intrinsic) {
        case TextIntrinsic::Len: {
            const auto target = builtin_type(draft, result);
            if (!target.has_value() || *target != BuiltinType::Usize) {
                return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
            }
            return finish_constant_integer(
                draft,
                result,
                IntegerConstant::from_parts(static_cast<std::uint64_t>(bytes.size()), false),
                *target
            );
        }
        case TextIntrinsic::IsEmpty: return constant_boolean(draft, result, bytes.empty());
        case TextIntrinsic::Bytes:
        case TextIntrinsic::Chars:
            return std::unexpected(ConstantEvaluationFailure::UnsupportedOperation);
    }
    std::unreachable();
}

auto fold_unary_constant(
    const ProgramDraft& draft,
    UnaryOperator operation,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto fact = load_constant_fact(draft, operand);
    if (!fact.has_value()) {
        return std::unexpected(fact.error());
    }
    return evaluate_unary_constant_value(draft, operation, *fact, result);
}

auto fold_binary_constant(
    const ProgramDraft& draft,
    BinaryOperator operation,
    std::optional<ConstantID> left,
    std::optional<ConstantID> right,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto left_fact = load_constant_fact(draft, left);
    if (!left_fact.has_value()) {
        return std::unexpected(left_fact.error());
    }
    const auto right_fact = load_constant_fact(draft, right);
    if (!right_fact.has_value()) {
        return std::unexpected(right_fact.error());
    }
    return evaluate_binary_constant_value(draft, operation, *left_fact, *right_fact, result);
}

auto fold_cast_constant(
    const ProgramDraft& draft,
    CastKind kind,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto fact = load_constant_fact(draft, operand);
    if (!fact.has_value()) {
        return std::unexpected(fact.error());
    }
    return evaluate_cast_constant_value(draft, kind, *fact, result);
}

auto fold_text_intrinsic_constant(
    const ProgramDraft& draft,
    TextIntrinsic intrinsic,
    std::optional<ConstantID> operand,
    TypeID result
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    const auto fact = load_constant_fact(draft, operand);
    if (!fact.has_value()) {
        return std::unexpected(fact.error());
    }
    return evaluate_text_intrinsic_constant_value(draft, intrinsic, *fact, result);
}

auto normalize_literal(
    ProgramDraft& draft,
    const ASTLiteral& literal,
    std::optional<ConstructionTypeRef> expected,
    LiteralSign sign
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
    validate_expected_type(draft, expected);
    const auto negative = sign == LiteralSign::Negative;
    return std::visit(
        [&](const auto& value) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure> {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, IntegerLiteralValue>) {
                if (value.conversion == NumericConversion::OutOfRange) {
                    return std::unexpected(ConstantEvaluationFailure::IntegerLiteralOutOfRange);
                }
                const auto type = normalized_numeric_type(draft, value.suffix, false, expected);
                const auto kind = builtin_type(draft, type);
                if (!kind.has_value() || !builtin_is_integer(*kind)) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                const auto integer = IntegerConstant::from_parts(value.magnitude, negative);
                if (!integer_constant_fits(integer, *kind)) {
                    return std::unexpected(
                        ConstantEvaluationFailure::IntegerLiteralNotRepresentable
                    );
                }
                return ConstantFact {.type = type, .value = integer};
            } else if constexpr (std::same_as<Value, FloatingLiteralValue>) {
                if (value.conversion == NumericConversion::OutOfRange) {
                    return std::unexpected(ConstantEvaluationFailure::FloatingLiteralOutOfRange);
                }
                const auto type = normalized_numeric_type(draft, value.suffix, true, expected);
                const auto kind = builtin_type(draft, type);
                if (!kind.has_value() || (*kind != BuiltinType::F32 && *kind != BuiltinType::F64)) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                const auto number = negative ? -value.value : value.value;
                if (*kind == BuiltinType::F32) {
                    const auto narrowed = static_cast<float>(number);
                    if (std::isfinite(number) && !std::isfinite(narrowed)) {
                        return std::unexpected(
                            ConstantEvaluationFailure::FloatingLiteralOutOfRange
                        );
                    }
                    return ConstantFact {
                        .type = type,
                        .value = F32Constant {.value = narrowed},
                    };
                }
                return ConstantFact {
                    .type = type,
                    .value = F64Constant {.value = number},
                };
            } else if constexpr (std::same_as<Value, BooleanLiteralValue>) {
                if (negative) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                const auto type = draft.intern_builtin_type(BuiltinType::Bool);
                return ConstantFact {
                    .type = type,
                    .value = BooleanConstant {.value = value.value},
                };
            } else if constexpr (std::same_as<Value, CharacterLiteralValue>) {
                if (negative) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                const auto type = draft.intern_builtin_type(BuiltinType::Char);
                return ConstantFact {
                    .type = type,
                    .value = CharacterConstant {.scalar = value.scalar},
                };
            } else if constexpr (std::same_as<Value, StringLiteralValue>) {
                if (negative) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                const auto spelling = draft.intern_spelling(value.bytes);
                const auto type = draft.intern_builtin_type(BuiltinType::Str);
                return ConstantFact {
                    .type = type,
                    .value = StringConstant {.value = spelling},
                };
            } else {
                static_assert(std::same_as<Value, void>, "new literal form requires normalization");
            }
        },
        literal.value
    );
}
