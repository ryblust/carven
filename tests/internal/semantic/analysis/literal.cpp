module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.literal;

import :diagnostics.code;
import :frontend.ast.literal;
import :frontend.literal;
import :semantic.analysis.constant.literal;
import :semantic.analysis.program;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.type;
import :source.text;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

auto integer_literal(
    std::uint64_t magnitude,
    NumericSuffix suffix = NumericSuffix::None,
    NumericConversion conversion = NumericConversion::Exact
) noexcept -> ASTLiteral {
    return ASTLiteral {
        .span = Span::at(0u),
        .value = IntegerLiteralValue {
            .value_span = Span::at(0u),
            .magnitude = magnitude,
            .base = IntegerBase::Decimal,
            .suffix = suffix,
            .conversion = conversion,
        },
    };
}

} // namespace

TEST_CASE("Semantic constant evaluation: literals normalize suffix, context, sign, and spelling") {
    auto fixture = ConstantEvaluationFixture();
    auto& compilation = fixture.compilation;
    const auto i32 = compilation.builtin_type(BuiltinType::I32);
    const auto i64 = compilation.builtin_type(BuiltinType::I64);
    const auto f32 = compilation.builtin_type(BuiltinType::F32);
    const auto f64 = compilation.builtin_type(BuiltinType::F64);

    const auto default_integer = normalize_literal(compilation, integer_literal(42u));
    REQUIRE(default_integer.has_value());
    CHECK_EQ(default_integer->type, i32);

    const auto contextual =
        normalize_literal(compilation, integer_literal(42u), ConstructionTypeRef {i64});
    REQUIRE(contextual.has_value());
    CHECK_EQ(contextual->type, i64);
    const auto contextual_value = std::get<IntegerConstant>(contextual->value).as_signed();
    REQUIRE(contextual_value.has_value());
    CHECK_EQ(*contextual_value, 42);

    const auto suffixed = normalize_literal(
        compilation,
        integer_literal(42u, NumericSuffix::I32),
        ConstructionTypeRef {i64}
    );
    REQUIRE(suffixed.has_value());
    CHECK_EQ(suffixed->type, i32);

    const auto signed_minimum = normalize_literal(
        compilation,
        integer_literal(128u, NumericSuffix::I8),
        std::nullopt,
        LiteralSign::Negative
    );
    REQUIRE(signed_minimum.has_value());
    const auto minimum_constant = std::get<IntegerConstant>(signed_minimum->value).as_signed();
    REQUIRE(minimum_constant.has_value());
    CHECK_EQ(*minimum_constant, -128);

    const auto positive_overflow =
        normalize_literal(compilation, integer_literal(128u, NumericSuffix::I8));
    REQUIRE_FALSE(positive_overflow.has_value());
    CHECK_EQ(positive_overflow.error(), ConstantEvaluationFailure::IntegerLiteralNotRepresentable);
    const auto range_diagnostic = constant_evaluation_diagnostic(positive_overflow.error());
    REQUIRE(range_diagnostic.has_value());
    CHECK_EQ(range_diagnostic->code, DiagnosticCode::ConstLiteralRange);

    const auto lexer_overflow = normalize_literal(
        compilation,
        integer_literal(0u, NumericSuffix::None, NumericConversion::OutOfRange)
    );
    REQUIRE_FALSE(lexer_overflow.has_value());
    CHECK_EQ(lexer_overflow.error(), ConstantEvaluationFailure::IntegerLiteralOutOfRange);
    REQUIRE(constant_evaluation_diagnostic(lexer_overflow.error()).has_value());
    CHECK_EQ(
        constant_evaluation_diagnostic(lexer_overflow.error())->code,
        DiagnosticCode::ConstOverflow
    );

    const auto floating = normalize_literal(
        compilation,
        ASTLiteral {
            .span = Span::at(0u),
            .value =
                FloatingLiteralValue {
                    .value_span = Span::at(0u),
                    .value = 1.25,
                    .suffix = NumericSuffix::None,
                    .conversion = NumericConversion::Exact,
                },
        },
        ConstructionTypeRef {f32}
    );
    REQUIRE(floating.has_value());
    CHECK_EQ(floating->type, f32);
    CHECK_EQ(std::get<F32Constant>(floating->value).value, 1.25f);

    const auto default_floating = normalize_literal(
        compilation,
        ASTLiteral {
            .span = Span::at(0u),
            .value = FloatingLiteralValue {
                .value_span = Span::at(0u),
                .value = 1.25,
                .suffix = NumericSuffix::None,
                .conversion = NumericConversion::Exact,
            },
        }
    );
    REQUIRE(default_floating.has_value());
    CHECK_EQ(default_floating->type, f64);

    const auto floating_overflow = normalize_literal(
        compilation,
        ASTLiteral {
            .span = Span::at(0u),
            .value = FloatingLiteralValue {
                .value_span = Span::at(0u),
                .value = std::numeric_limits<double>::max(),
                .suffix = NumericSuffix::F32,
                .conversion = NumericConversion::Exact,
            },
        }
    );
    REQUIRE_FALSE(floating_overflow.has_value());
    CHECK_EQ(floating_overflow.error(), ConstantEvaluationFailure::FloatingLiteralOutOfRange);

    const auto string = normalize_literal(
        compilation,
        ASTLiteral {
            .span = Span::at(0u),
            .value = StringLiteralValue {.bytes = "hello"},
        }
    );
    REQUIRE(string.has_value());
    const auto spelling = std::get<StringConstant>(string->value).value;
    CHECK_EQ(compilation.spelling_copy(spelling), "hello");
    CHECK_EQ(std::get<StringConstant>(string->value).value, spelling);
}
