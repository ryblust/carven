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
import :test.harness.framework;
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

const TestSuite suite([] static noexcept {
    "Semantic constant facts: literals normalize suffix, context, sign, and spelling"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& compilation = fixture.compilation;
            const auto i32 = compilation.builtin_type(BuiltinType::I32);
            const auto i64 = compilation.builtin_type(BuiltinType::I64);
            const auto f32 = compilation.builtin_type(BuiltinType::F32);
            const auto f64 = compilation.builtin_type(BuiltinType::F64);

            const auto default_integer = normalize_literal(compilation, integer_literal(42u));
            if (!expect(default_integer.has_value())) {
                return;
            }
            expect(((default_integer->type) == (i32))).note("default_integer->type == i32");

            const auto contextual =
                normalize_literal(compilation, integer_literal(42u), ConstructionTypeRef {i64});
            if (!expect(contextual.has_value())) {
                return;
            }
            expect(((contextual->type) == (i64))).note("contextual->type == i64");
            const auto contextual_value = std::get<IntegerConstant>(contextual->value).as_signed();
            if (!expect(contextual_value.has_value())) {
                return;
            }
            expect_equal(*contextual_value, 42);

            const auto suffixed = normalize_literal(
                compilation,
                integer_literal(42u, NumericSuffix::I32),
                ConstructionTypeRef {i64}
            );
            if (!expect(suffixed.has_value())) {
                return;
            }
            expect(((suffixed->type) == (i32))).note("suffixed->type == i32");

            const auto signed_minimum = normalize_literal(
                compilation,
                integer_literal(128u, NumericSuffix::I8),
                std::nullopt,
                LiteralSign::Negative
            );
            if (!expect(signed_minimum.has_value())) {
                return;
            }
            const auto minimum_constant =
                std::get<IntegerConstant>(signed_minimum->value).as_signed();
            if (!expect(minimum_constant.has_value())) {
                return;
            }
            expect_equal(*minimum_constant, -128);

            const auto positive_overflow =
                normalize_literal(compilation, integer_literal(128u, NumericSuffix::I8));
            if (!expect(!(positive_overflow.has_value()))) {
                return;
            }
            expect_equal(
                positive_overflow.error(),
                ConstantEvaluationFailure::IntegerLiteralNotRepresentable
            );
            const auto range_diagnostic = constant_evaluation_diagnostic(positive_overflow.error());
            if (!expect(range_diagnostic.has_value())) {
                return;
            }
            expect_equal(range_diagnostic->code, DiagnosticCode::ConstLiteralRange);

            const auto lexer_overflow = normalize_literal(
                compilation,
                integer_literal(0u, NumericSuffix::None, NumericConversion::OutOfRange)
            );
            if (!expect(!(lexer_overflow.has_value()))) {
                return;
            }
            expect_equal(
                lexer_overflow.error(),
                ConstantEvaluationFailure::IntegerLiteralOutOfRange
            );
            if (!expect(constant_evaluation_diagnostic(lexer_overflow.error()).has_value())) {
                return;
            }
            expect_equal(
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
                            .spelling = "1.25",
                            .suffix = NumericSuffix::None,
                        },
                },
                ConstructionTypeRef {f32}
            );
            if (!expect(floating.has_value())) {
                return;
            }
            expect(((floating->type) == (f32))).note("floating->type == f32");
            expect_equal(std::get<F32Constant>(floating->value).value, 1.25f);

            const auto default_floating = normalize_literal(
                compilation,
                ASTLiteral {
                    .span = Span::at(0u),
                    .value = FloatingLiteralValue {
                        .value_span = Span::at(0u),
                        .spelling = "1.25",
                        .suffix = NumericSuffix::None,
                    },
                }
            );
            if (!expect(default_floating.has_value())) {
                return;
            }
            expect(((default_floating->type) == (f64))).note("default_floating->type == f64");

            const auto floating_overflow = normalize_literal(
                compilation,
                ASTLiteral {
                    .span = Span::at(0u),
                    .value = FloatingLiteralValue {
                        .value_span = Span::at(0u),
                        .spelling = "1.7976931348623157e308",
                        .suffix = NumericSuffix::F32,
                    },
                }
            );
            if (!expect(!(floating_overflow.has_value()))) {
                return;
            }
            expect_equal(
                floating_overflow.error(),
                ConstantEvaluationFailure::FloatingLiteralOutOfRange
            );

            const auto string = normalize_literal(
                compilation,
                ASTLiteral {
                    .span = Span::at(0u),
                    .value = StringLiteralValue {.bytes = "hello"},
                }
            );
            if (!expect(string.has_value())) {
                return;
            }
            const auto spelling = std::get<StringConstant>(string->value).value;
            expect_equal(compilation.spelling_copy(spelling), std::string_view("hello"));
            expect(((std::get<StringConstant>(string->value).value) == (spelling)))
                .note("std::get<StringConstant>(string->value).value == spelling");
        };

    "Semantic literals: floating conversion uses the selected precision and range"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& compilation = fixture.compilation;
            const auto f32 = compilation.builtin_type(BuiltinType::F32);
            const auto f64 = compilation.builtin_type(BuiltinType::F64);

            struct Case final {
                std::string_view spelling;
                NumericSuffix suffix;
                BuiltinType expected_type;
                std::optional<std::uint64_t> bits;
            };

            const auto cases = std::array {
                Case {
                    .spelling = "1.0000000596046447",
                    .suffix = NumericSuffix::F32,
                    .expected_type = BuiltinType::F64,
                    .bits = 0x3f800000u
                },
                Case {
                    .spelling = "1.000000059604644775390625",
                    .suffix = NumericSuffix::F32,
                    .expected_type = BuiltinType::F64,
                    .bits = 0x3f800000u
                },
                Case {
                    .spelling = "1.0000000596046448",
                    .suffix = NumericSuffix::F32,
                    .expected_type = BuiltinType::F64,
                    .bits = 0x3f800001u
                },
                Case {
                    .spelling = "1.0000000596046448",
                    .suffix = NumericSuffix::None,
                    .expected_type = BuiltinType::F32,
                    .bits = 0x3f800001u
                },
                Case {
                    .spelling = "1.0000000000000002",
                    .suffix = NumericSuffix::F64,
                    .expected_type = BuiltinType::F32,
                    .bits = 0x3ff0000000000001ull
                },
                Case {
                    .spelling = "1.401298464324817e-45",
                    .suffix = NumericSuffix::F32,
                    .expected_type = BuiltinType::F32,
                    .bits = 1u
                },
                Case {
                    .spelling = "5e-324",
                    .suffix = NumericSuffix::F64,
                    .expected_type = BuiltinType::F64,
                    .bits = 1u
                },
                Case {
                    .spelling = "0.0",
                    .suffix = NumericSuffix::F32,
                    .expected_type = BuiltinType::F32,
                    .bits = 0u
                },
                Case {
                    .spelling = "3.5e38",
                    .suffix = NumericSuffix::F32,
                    .expected_type = BuiltinType::F32,
                    .bits = std::nullopt
                },
                Case {
                    .spelling = "1e-50",
                    .suffix = NumericSuffix::None,
                    .expected_type = BuiltinType::F32,
                    .bits = std::nullopt
                },
                Case {
                    .spelling = "1e309",
                    .suffix = NumericSuffix::F64,
                    .expected_type = BuiltinType::F64,
                    .bits = std::nullopt
                },
                Case {
                    .spelling = "1e-400",
                    .suffix = NumericSuffix::F64,
                    .expected_type = BuiltinType::F64,
                    .bits = std::nullopt
                },
            };
            const auto signs = std::array {LiteralSign::Positive, LiteralSign::Negative};
            each(cases, &Case::spelling, [&](const auto& item) noexcept {
                each(
                    signs,
                    [](LiteralSign sign) static noexcept -> std::string_view {
                        return sign == LiteralSign::Positive ? "positive" : "negative";
                    },
                    [&](LiteralSign sign) noexcept {
                        const auto result = normalize_literal(
                            compilation,
                            ASTLiteral {
                                .span = Span::at(0u),
                                .value =
                                    FloatingLiteralValue {
                                        .value_span = Span::at(0u),
                                        .spelling = std::string(item.spelling),
                                        .suffix = item.suffix,
                                    },
                            },
                            ConstructionTypeRef {compilation.builtin_type(item.expected_type)},
                            sign
                        );
                        if (!item.bits.has_value()) {
                            if (!(expect(!(result.has_value()))
                                      .note("item.suffix = ", static_cast<int>(item.suffix)))) {
                                return;
                            }
                            expect_equal(
                                result.error(),
                                ConstantEvaluationFailure::FloatingLiteralOutOfRange
                            )
                                .note("item.suffix = ", static_cast<int>(item.suffix));
                            return;
                        }
                        if (!(expect(result.has_value())
                                  .note("item.suffix = ", static_cast<int>(item.suffix)))) {
                            return;
                        }
                        const auto narrow = item.suffix == NumericSuffix::F32
                            || (item.suffix == NumericSuffix::None
                                && item.expected_type == BuiltinType::F32);
                        expect(((result->type) == (narrow ? f32 : f64)))
                            .note(
                                "result->type == narrow ? f32 : f64",
                                "item.suffix = ",
                                static_cast<int>(item.suffix)
                            );
                        const auto sign_bit = sign == LiteralSign::Negative
                            ? (narrow ? 0x80000000ull : 0x8000000000000000ull)
                            : 0ull;
                        const auto bits = narrow ? std::uint64_t {std::bit_cast<std::uint32_t>(
                                                       std::get<F32Constant>(result->value).value
                                                   )}
                                                 : std::bit_cast<std::uint64_t>(
                                                       std::get<F64Constant>(result->value).value
                                                   );
                        expect(((bits) == (*item.bits | sign_bit)))
                            .note(
                                "bits == *item.bits | sign_bit",
                                "item.suffix = ",
                                static_cast<int>(item.suffix)
                            );
                    }
                );
            });
        };
});

} // namespace
