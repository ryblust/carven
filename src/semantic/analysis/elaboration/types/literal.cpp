module carven:semantic.analysis.elaboration.types.literal.impl;

import :frontend.ast.literal;
import :frontend.literal;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.types;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.type;
import std;

namespace {

auto numeric_literal_fact(
    ModuleAnalysis& module_analysis,
    Span span,
    const NumericLiteralValue& literal,
    HIRTypeID resolved_type,
    bool negative
) noexcept -> LiteralFact {
    const auto& builder = module_analysis.builder();
    if (const auto* floating = std::get_if<FloatingLiteralValue>(&literal)) {
        const auto* builtin_type =
            std::get_if<HIRBuiltinTypeValue>(&builder.type(resolved_type).value);
        const auto is_f32 = builtin_type != nullptr && builtin_type->kind == HIRBuiltinType::F32;
        auto value = negative ? -floating->value : floating->value;
        auto out_of_range = floating->conversion == NumericConversion::OutOfRange;
        if (!out_of_range && is_f32) {
            const auto narrowed = static_cast<float>(value);
            out_of_range = std::isfinite(value) && !std::isfinite(narrowed);
            value = narrowed;
        }
        if (out_of_range) {
            module_analysis.emit(
                floating->value_span,
                "floating constant is out of range",
                DiagnosticCode::ConstOverflow
            );
            return {
                .constant = std::nullopt,
                .value = is_f32 ? HIRLiteralValue {HIRF32LiteralValue {.value = 0}}
                                : HIRLiteralValue {HIRF64LiteralValue {.value = 0}},
            };
        }
        return {
            .constant = HIRFloatingConstant {.value = value},
            .value = is_f32 ? HIRLiteralValue {HIRF32LiteralValue {
                                  .value = static_cast<float>(value),
                              }}
                            : HIRLiteralValue {HIRF64LiteralValue {.value = value}},
        };
    }

    const auto& integer = std::get<IntegerLiteralValue>(literal);
    if (integer.conversion == NumericConversion::OutOfRange) {
        module_analysis.emit(
            integer.value_span,
            "integer constant is out of range",
            DiagnosticCode::ConstOverflow
        );
        return {
            .constant = std::nullopt,
            .value = HIRIntegerLiteralValue {
                .negative = false,
                .magnitude = 0,
            },
        };
    }
    const auto constant =
        HIRIntegerConstant::from_parts(integer.magnitude, negative && integer.magnitude != 0);
    if (is_integer(module_analysis, resolved_type)
        && !integer_constant_fits(module_analysis, constant, resolved_type)) {
        module_analysis.emit(
            span,
            "integer literal is not representable in its type",
            DiagnosticCode::ConstLiteralRange
        );
        return {
            .constant = std::nullopt,
            .value = HIRIntegerLiteralValue {
                .negative = constant.negative(),
                .magnitude = integer.magnitude,
            },
        };
    }
    return {
        .constant = constant,
        .value = HIRIntegerLiteralValue {
            .negative = constant.negative(),
            .magnitude = integer.magnitude,
        },
    };
}

} // namespace

auto as_numeric_literal(const ASTLiteral& literal) noexcept -> std::optional<NumericLiteralValue> {
    if (const auto* integer = std::get_if<IntegerLiteralValue>(&literal.value)) {
        return NumericLiteralValue {*integer};
    }
    if (const auto* floating = std::get_if<FloatingLiteralValue>(&literal.value)) {
        return NumericLiteralValue {*floating};
    }
    return std::nullopt;
}

auto numeric_literal_type(
    ModuleAnalysis& module_analysis,
    Span span,
    const NumericLiteralValue& value
) noexcept -> HIRTypeID {
    switch (numeric_suffix(value)) {
        case NumericSuffix::I8:    return builtin(module_analysis, span, HIRBuiltinType::I8);
        case NumericSuffix::I16:   return builtin(module_analysis, span, HIRBuiltinType::I16);
        case NumericSuffix::I32:   return builtin(module_analysis, span, HIRBuiltinType::I32);
        case NumericSuffix::I64:   return builtin(module_analysis, span, HIRBuiltinType::I64);
        case NumericSuffix::U8:    return builtin(module_analysis, span, HIRBuiltinType::U8);
        case NumericSuffix::U16:   return builtin(module_analysis, span, HIRBuiltinType::U16);
        case NumericSuffix::U32:   return builtin(module_analysis, span, HIRBuiltinType::U32);
        case NumericSuffix::U64:   return builtin(module_analysis, span, HIRBuiltinType::U64);
        case NumericSuffix::Isize: return builtin(module_analysis, span, HIRBuiltinType::Isize);
        case NumericSuffix::Usize: return builtin(module_analysis, span, HIRBuiltinType::Usize);
        case NumericSuffix::F32:   return builtin(module_analysis, span, HIRBuiltinType::F32);
        case NumericSuffix::F64:   return builtin(module_analysis, span, HIRBuiltinType::F64);
        case NumericSuffix::None:  break;
    }
    return std::holds_alternative<FloatingLiteralValue>(value)
        ? builtin(module_analysis, span, HIRBuiltinType::F64)
        : builtin(module_analysis, span, HIRBuiltinType::I32);
}

auto literal_type(ModuleAnalysis& module_analysis, const ASTLiteral& literal) noexcept
    -> HIRTypeID {
    if (const auto numeric = as_numeric_literal(literal)) {
        return numeric_literal_type(module_analysis, literal.span, *numeric);
    }
    if (std::holds_alternative<BooleanLiteralValue>(literal.value)) {
        return builtin(module_analysis, literal.span, HIRBuiltinType::Bool);
    }
    if (std::holds_alternative<CharacterLiteralValue>(literal.value)) {
        return builtin(module_analysis, literal.span, HIRBuiltinType::Char);
    }
    return builtin(module_analysis, literal.span, HIRBuiltinType::Str);
}

auto literal_fact(
    ModuleAnalysis& module_analysis,
    const ASTLiteral& literal,
    HIRTypeID resolved_type
) noexcept -> LiteralFact {
    if (const auto numeric = as_numeric_literal(literal)) {
        return numeric_literal_fact(module_analysis, literal.span, *numeric, resolved_type, false);
    }
    if (const auto* boolean = std::get_if<BooleanLiteralValue>(&literal.value)) {
        return {
            .constant = HIRBooleanConstant {.value = boolean->value},
            .value = HIRBooleanLiteralValue {.value = boolean->value},
        };
    }
    if (const auto* string = std::get_if<StringLiteralValue>(&literal.value)) {
        const auto bytes = module_analysis.builder().intern_string(string->bytes);
        return {
            .constant = HIRStringConstant {.value = bytes},
            .value = HIRStrLiteralValue {.bytes = bytes},
        };
    }
    const auto& character = std::get<CharacterLiteralValue>(literal.value);
    return {
        .constant = HIRCharacterConstant {.scalar = character.scalar},
        .value = HIRCharacterLiteralValue {.scalar = character.scalar},
    };
}

auto negative_literal_fact(
    ModuleAnalysis& module_analysis,
    Span span,
    const NumericLiteralValue& value,
    HIRTypeID resolved_type
) noexcept -> LiteralFact {
    return numeric_literal_fact(module_analysis, span, value, resolved_type, true);
}
