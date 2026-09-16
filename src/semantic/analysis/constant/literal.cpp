module carven:semantic.analysis.constant.literal.impl;

import :frontend.ast.literal;
import :frontend.literal;
import :semantic.analysis.constant.literal;
import :semantic.analysis.program;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.type;
import std;

namespace {

auto builtin_type(const ProgramDraft& draft, TypeID type) noexcept -> std::optional<BuiltinType> {
    const auto canonical = draft.type_copy(type);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    return builtin == nullptr ? std::nullopt : std::optional(builtin->kind);
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
    const ProgramDraft& draft,
    NumericSuffix suffix,
    bool floating,
    std::optional<ConstructionTypeRef> expected
) noexcept -> TypeID {
    if (const auto suffixed = suffix_builtin(suffix)) {
        return draft.builtin_type(*suffixed);
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
    return draft.builtin_type(floating ? BuiltinType::F64 : BuiltinType::I32);
}

} // namespace

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
            } else if constexpr (std::same_as<Value, NullPointerLiteralValue>) {
                const auto* type = expected ? std::get_if<TypeID>(&*expected) : nullptr;
                if (negative
                    || type == nullptr
                    || !std::holds_alternative<PointerTypeValue>(draft.type_copy(*type).value)) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                return ConstantFact {.type = *type, .value = NullPointerConstant {}};
            } else if constexpr (std::same_as<Value, BooleanLiteralValue>) {
                if (negative) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                const auto type = draft.builtin_type(BuiltinType::Bool);
                return ConstantFact {
                    .type = type,
                    .value = BooleanConstant {.value = value.value},
                };
            } else if constexpr (std::same_as<Value, CharacterLiteralValue>) {
                if (negative) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                const auto type = draft.builtin_type(BuiltinType::Char);
                return ConstantFact {
                    .type = type,
                    .value = CharacterConstant {.scalar = value.scalar},
                };
            } else if constexpr (std::same_as<Value, CStringLiteralValue>) {
                return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
            } else if constexpr (std::same_as<Value, StringLiteralValue>) {
                if (negative) {
                    return std::unexpected(ConstantEvaluationFailure::InvalidOperation);
                }
                const auto spelling = draft.intern_spelling(value.bytes);
                const auto type = draft.builtin_type(BuiltinType::Str);
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
