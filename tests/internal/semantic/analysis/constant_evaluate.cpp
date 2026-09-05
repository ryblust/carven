module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.constant_evaluate;

import :compiler.request;
import :diagnostics.code;
import :diagnostics.sink;
import :frontend.ast.literal;
import :frontend.literal;
import :frontend.program.parse;
import :semantic.analysis.constant.evaluate;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.harness.death;
import std;

namespace {

auto path(std::string_view value) noexcept -> CanonicalModulePath {
    auto result = CanonicalModulePath::from_value(value);
    REQUIRE(result.has_value());
    return std::move(*result);
}

auto begin_compilation(SourceManager& sources, DiagnosticSink& diagnostics) noexcept
    -> ProgramDraft {
    const auto source = sources.append_virtual("constant-evaluate.cv", "");
    REQUIRE(source.has_value());
    const auto inputs = std::array {
        CompilationModuleInput {
            .source_id = *source,
            .module_path = path("constant.evaluate"),
        },
    };
    auto syntax = parse_program(sources, CompilationRequest {.modules = inputs});
    REQUIRE(syntax.has_value());
    return ProgramDraft::begin(std::move(*syntax), diagnostics);
}

struct EvaluationFixture final {
    SourceManager sources;
    DiagnosticSink diagnostics;
    ProgramDraft compilation;

    EvaluationFixture() noexcept
        : sources(),
          diagnostics(),
          compilation(begin_compilation(sources, diagnostics)) {}
};

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

auto integer_fact(TypeID type, std::int64_t value) noexcept -> ConstantFact {
    return ConstantFact {.type = type, .value = IntegerConstant::from_signed(value)};
}

} // namespace

TEST_CASE("Semantic constant evaluation: literals normalize suffix, context, sign, and spelling") {
    auto fixture = EvaluationFixture();
    auto& compilation = fixture.compilation;
    const auto i32 = compilation.intern_builtin_type(BuiltinType::I32);
    const auto i64 = compilation.intern_builtin_type(BuiltinType::I64);
    const auto f32 = compilation.intern_builtin_type(BuiltinType::F32);
    const auto f64 = compilation.intern_builtin_type(BuiltinType::F64);

    const auto default_integer = normalize_literal(compilation, integer_literal(42u));
    REQUIRE(default_integer.has_value());
    CHECK_EQ(default_integer->constant.type, i32);

    const auto contextual =
        normalize_literal(compilation, integer_literal(42u), ConstructionTypeRef {i64});
    REQUIRE(contextual.has_value());
    CHECK_EQ(contextual->constant.type, i64);
    const auto contextual_value = std::get<IntegerConstant>(contextual->constant.value).as_signed();
    REQUIRE(contextual_value.has_value());
    CHECK_EQ(*contextual_value, 42);

    const auto suffixed = normalize_literal(
        compilation,
        integer_literal(42u, NumericSuffix::I32),
        ConstructionTypeRef {i64}
    );
    REQUIRE(suffixed.has_value());
    CHECK_EQ(suffixed->constant.type, i32);

    const auto signed_minimum = normalize_literal(
        compilation,
        integer_literal(128u, NumericSuffix::I8),
        std::nullopt,
        LiteralSign::Negative
    );
    REQUIRE(signed_minimum.has_value());
    const auto minimum_constant =
        std::get<IntegerConstant>(signed_minimum->constant.value).as_signed();
    const auto minimum_literal =
        std::get<IntegerLiteral>(signed_minimum->literal).value.as_signed();
    REQUIRE(minimum_constant.has_value());
    REQUIRE(minimum_literal.has_value());
    CHECK_EQ(*minimum_constant, -128);
    CHECK_EQ(*minimum_literal, -128);

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
    CHECK_EQ(floating->constant.type, f32);
    CHECK_EQ(std::get<F32Constant>(floating->constant.value).value, 1.25f);
    CHECK(std::holds_alternative<F32Literal>(floating->literal));

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
    CHECK_EQ(default_floating->constant.type, f64);
    CHECK(std::holds_alternative<F64Literal>(default_floating->literal));

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
    const auto spelling = std::get<StringConstant>(string->constant.value).value;
    CHECK_EQ(compilation.spelling_copy(spelling), "hello");
    CHECK_EQ(std::get<StringLiteral>(string->literal).bytes, spelling);
}

TEST_CASE("Semantic constant evaluation: checked integer folds preserve diagnostic classes") {
    auto fixture = EvaluationFixture();
    auto& compilation = fixture.compilation;
    const auto boolean = compilation.intern_builtin_type(BuiltinType::Bool);
    const auto i8 = compilation.intern_builtin_type(BuiltinType::I8);
    const auto u8 = compilation.intern_builtin_type(BuiltinType::U8);

    const auto maximum = compilation.intern_constant(integer_fact(i8, 127));
    const auto one_i8 = compilation.intern_constant(integer_fact(i8, 1));
    const auto overflow =
        fold_binary_constant(compilation, BinaryOperator::Add, maximum, one_i8, i8);
    REQUIRE_FALSE(overflow.has_value());
    CHECK_EQ(overflow.error(), ConstantEvaluationFailure::IntegerOverflow);

    const auto zero_i8 = compilation.intern_constant(integer_fact(i8, 0));
    const auto divide_by_zero =
        fold_binary_constant(compilation, BinaryOperator::Divide, one_i8, zero_i8, i8);
    REQUIRE_FALSE(divide_by_zero.has_value());
    CHECK_EQ(divide_by_zero.error(), ConstantEvaluationFailure::DivideByZero);
    REQUIRE(constant_evaluation_diagnostic(divide_by_zero.error()).has_value());
    CHECK_EQ(
        constant_evaluation_diagnostic(divide_by_zero.error())->code,
        DiagnosticCode::ConstDivideByZero
    );

    const auto one_u8 = compilation.intern_constant(
        ConstantFact {
            .type = u8,
            .value = IntegerConstant::from_parts(1u, false),
        }
    );
    const auto eight_u8 = compilation.intern_constant(
        ConstantFact {
            .type = u8,
            .value = IntegerConstant::from_parts(8u, false),
        }
    );
    const auto bad_shift =
        fold_binary_constant(compilation, BinaryOperator::LeftShift, one_u8, eight_u8, u8);
    REQUIRE_FALSE(bad_shift.has_value());
    CHECK_EQ(bad_shift.error(), ConstantEvaluationFailure::ShiftOutOfRange);

    const auto equal =
        fold_binary_constant(compilation, BinaryOperator::Equal, one_i8, one_i8, boolean);
    REQUIRE(equal.has_value());
    CHECK(std::get<BooleanConstant>(equal->value).value);

    const auto absent = fold_unary_constant(compilation, UnaryOperator::Negate, std::nullopt, i8);
    REQUIRE_FALSE(absent.has_value());
    CHECK_EQ(absent.error(), ConstantEvaluationFailure::OperandNotConstant);
    CHECK_FALSE(constant_evaluation_diagnostic(absent.error()).has_value());
}

TEST_CASE("Semantic constant evaluation: casts and text intrinsics return canonical facts") {
    auto fixture = EvaluationFixture();
    auto& compilation = fixture.compilation;
    const auto i8 = compilation.intern_builtin_type(BuiltinType::I8);
    const auto u8 = compilation.intern_builtin_type(BuiltinType::U8);
    const auto usize = compilation.intern_builtin_type(BuiltinType::Usize);
    const auto boolean = compilation.intern_builtin_type(BuiltinType::Bool);
    const auto f32 = compilation.intern_builtin_type(BuiltinType::F32);
    const auto f64 = compilation.intern_builtin_type(BuiltinType::F64);

    const auto negative_one = compilation.intern_constant(integer_fact(i8, -1));
    const auto wrapped =
        fold_cast_constant(compilation, CastKind::IntegerToInteger, negative_one, u8);
    REQUIRE(wrapped.has_value());
    const auto wrapped_value = std::get<IntegerConstant>(wrapped->value).as_unsigned();
    REQUIRE(wrapped_value.has_value());
    CHECK_EQ(*wrapped_value, 255u);

    const auto narrow_float = compilation.intern_constant(
        ConstantFact {
            .type = f32,
            .value = F32Constant {.value = 1.5f},
        }
    );
    const auto widened =
        fold_cast_constant(compilation, CastKind::FloatingWiden, narrow_float, f64);
    REQUIRE(widened.has_value());
    CHECK_EQ(std::get<F64Constant>(widened->value).value, 1.5);

    const auto string = normalize_literal(
        compilation,
        ASTLiteral {
            .span = Span::at(0u),
            .value = StringLiteralValue {.bytes = "abc"},
        }
    );
    REQUIRE(string.has_value());
    const auto string_id = compilation.intern_constant(string->constant);
    const auto length =
        fold_text_intrinsic_constant(compilation, TextIntrinsic::Len, string_id, usize);
    REQUIRE(length.has_value());
    const auto length_value = std::get<IntegerConstant>(length->value).as_unsigned();
    REQUIRE(length_value.has_value());
    CHECK_EQ(*length_value, 3u);
    const auto empty =
        fold_text_intrinsic_constant(compilation, TextIntrinsic::IsEmpty, string_id, boolean);
    REQUIRE(empty.has_value());
    CHECK_FALSE(std::get<BooleanConstant>(empty->value).value);
    const auto view = fold_text_intrinsic_constant(
        compilation,
        TextIntrinsic::Bytes,
        string_id,
        string->constant.type
    );
    REQUIRE_FALSE(view.has_value());
    CHECK_EQ(view.error(), ConstantEvaluationFailure::UnsupportedOperation);
}

TEST_CASE("Semantic constant evaluation: operand facts retain program owner evidence") {
    auto first = EvaluationFixture();
    auto second = EvaluationFixture();
    const auto first_i32 = first.compilation.intern_builtin_type(BuiltinType::I32);
    const auto second_i32 = second.compilation.intern_builtin_type(BuiltinType::I32);
    const auto foreign = integer_fact(first_i32, 1);
    CHECK(expect_termination("semantic-constant-foreign-owner", [&] {
        static_cast<void>(evaluate_unary_constant_value(
            second.compilation,
            UnaryOperator::Negate,
            foreign,
            second_i32
        ));
    }));
}
