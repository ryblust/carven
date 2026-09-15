module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.evaluation.operation;

import :diagnostics.code;
import :semantic.evaluation.operation;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.operation;
import :semantic.semir.type;
import :test.internal.harness.death;
import :test.internal.semantic.evaluation.fixture;
import std;

TEST_CASE("Semantic constant evaluation: checked integer folds preserve diagnostic classes") {
    auto fixture = ConstantEvaluationFixture();
    ConstantValueAccess& values = fixture.compilation;
    const auto boolean = values.intern_builtin_type(BuiltinType::Bool);
    const auto i8 = values.intern_builtin_type(BuiltinType::I8);
    const auto u8 = values.intern_builtin_type(BuiltinType::U8);

    const auto maximum = values.intern_constant(constant_test_integer_fact(i8, 127));
    const auto one_i8 = values.intern_constant(constant_test_integer_fact(i8, 1));
    const auto overflow = fold_binary_constant(values, BinaryOperator::Add, maximum, one_i8, i8);
    REQUIRE_FALSE(overflow.has_value());
    CHECK_EQ(overflow.error(), ConstantEvaluationFailure::IntegerOverflow);

    const auto zero_i8 = values.intern_constant(constant_test_integer_fact(i8, 0));
    const auto divide_by_zero =
        fold_binary_constant(values, BinaryOperator::Divide, one_i8, zero_i8, i8);
    REQUIRE_FALSE(divide_by_zero.has_value());
    CHECK_EQ(divide_by_zero.error(), ConstantEvaluationFailure::DivideByZero);
    REQUIRE(constant_evaluation_diagnostic(divide_by_zero.error()).has_value());
    CHECK_EQ(
        constant_evaluation_diagnostic(divide_by_zero.error())->code,
        DiagnosticCode::ConstDivideByZero
    );

    const auto one_u8 = values.intern_constant(
        ConstantFact {
            .type = u8,
            .value = IntegerConstant::from_parts(1u, false),
        }
    );
    const auto eight_u8 = values.intern_constant(
        ConstantFact {
            .type = u8,
            .value = IntegerConstant::from_parts(8u, false),
        }
    );
    const auto bad_shift =
        fold_binary_constant(values, BinaryOperator::LeftShift, one_u8, eight_u8, u8);
    REQUIRE_FALSE(bad_shift.has_value());
    CHECK_EQ(bad_shift.error(), ConstantEvaluationFailure::ShiftOutOfRange);

    const auto equal = fold_binary_constant(values, BinaryOperator::Equal, one_i8, one_i8, boolean);
    REQUIRE(equal.has_value());
    CHECK(std::get<BooleanConstant>(equal->value).value);

    const auto absent = fold_unary_constant(values, UnaryOperator::Negate, std::nullopt, i8);
    REQUIRE_FALSE(absent.has_value());
    CHECK_EQ(absent.error(), ConstantEvaluationFailure::OperandNotConstant);
    CHECK_FALSE(constant_evaluation_diagnostic(absent.error()).has_value());
}

TEST_CASE("Semantic constant evaluation: casts and text intrinsics return canonical facts") {
    auto fixture = ConstantEvaluationFixture();
    ConstantValueAccess& values = fixture.compilation;
    const auto i8 = values.intern_builtin_type(BuiltinType::I8);
    const auto u8 = values.intern_builtin_type(BuiltinType::U8);
    const auto usize = values.intern_builtin_type(BuiltinType::Usize);
    const auto boolean = values.intern_builtin_type(BuiltinType::Bool);
    const auto f32 = values.intern_builtin_type(BuiltinType::F32);
    const auto f64 = values.intern_builtin_type(BuiltinType::F64);

    const auto negative_one = values.intern_constant(constant_test_integer_fact(i8, -1));
    const auto wrapped = fold_cast_constant(values, CastKind::IntegerToInteger, negative_one, u8);
    REQUIRE(wrapped.has_value());
    const auto wrapped_value = std::get<IntegerConstant>(wrapped->value).as_unsigned();
    REQUIRE(wrapped_value.has_value());
    CHECK_EQ(*wrapped_value, 255u);

    const auto narrow_float = values.intern_constant(
        ConstantFact {
            .type = f32,
            .value = F32Constant {.value = 1.5f},
        }
    );
    const auto widened = fold_cast_constant(values, CastKind::FloatingWiden, narrow_float, f64);
    REQUIRE(widened.has_value());
    CHECK_EQ(std::get<F64Constant>(widened->value).value, 1.5);

    const auto string_id = values.intern_constant(
        ConstantFact {
            .type = values.intern_builtin_type(BuiltinType::Str),
            .value = StringConstant {.value = values.intern_spelling("abc")},
        }
    );
    const auto string = load_constant_fact(values, string_id);
    REQUIRE(string.has_value());
    const auto length = fold_text_intrinsic_constant(values, TextIntrinsic::Len, string_id, usize);
    REQUIRE(length.has_value());
    const auto length_value = std::get<IntegerConstant>(length->value).as_unsigned();
    REQUIRE(length_value.has_value());
    CHECK_EQ(*length_value, 3u);
    const auto empty =
        fold_text_intrinsic_constant(values, TextIntrinsic::IsEmpty, string_id, boolean);
    REQUIRE(empty.has_value());
    CHECK_FALSE(std::get<BooleanConstant>(empty->value).value);
    const auto view =
        fold_text_intrinsic_constant(values, TextIntrinsic::Bytes, string_id, (*string)->type);
    REQUIRE_FALSE(view.has_value());
    CHECK_EQ(view.error(), ConstantEvaluationFailure::UnsupportedOperation);
}

TEST_CASE("Semantic constant evaluation: operand facts retain program owner evidence") {
    auto first = ConstantEvaluationFixture();
    auto second = ConstantEvaluationFixture();
    ConstantValueAccess& first_values = first.compilation;
    ConstantValueAccess& second_values = second.compilation;
    const auto first_i32 = first_values.intern_builtin_type(BuiltinType::I32);
    const auto second_i32 = second_values.intern_builtin_type(BuiltinType::I32);
    const auto foreign = constant_test_integer_fact(first_i32, 1);
    CHECK(expect_termination("semantic-constant-foreign-owner", [&] {
        static_cast<void>(
            evaluate_unary_constant_value(second_values, UnaryOperator::Negate, foreign, second_i32)
        );
    }));
}

TEST_CASE(
    "Semantic constants: floating identity preserves signed zero while equality compares values"
) {
    auto fixture = ConstantEvaluationFixture();
    ConstantValueAccess& values = fixture.compilation;
    const auto check_zero = [&]<typename Floating>(BuiltinType builtin) noexcept {
        const auto type = values.intern_builtin_type(builtin);
        const auto positive = ConstantFact {.type = type, .value = Floating {.value = 0.0}};
        const auto negative = ConstantFact {.type = type, .value = Floating {.value = -0.0}};
        const auto positive_id = values.intern_constant(positive);
        const auto negative_id = values.intern_constant(negative);
        CHECK_NE(positive_id, negative_id);
        CHECK_EQ(values.intern_constant(negative), negative_id);
        CHECK(constant_value_equal(values, positive.value, negative.value));
        CHECK(std::signbit(std::get<Floating>(values.constant(negative_id).value).value));
    };
    check_zero.operator()<F32Constant>(BuiltinType::F32);
    check_zero.operator()<F64Constant>(BuiltinType::F64);
}

TEST_CASE("Semantic execution: runtime integer arithmetic wraps at the operand width") {
    auto fixture = ConstantEvaluationFixture();
    auto& values = fixture.compilation;

    struct Scenario final {
        BuiltinType type;
        BinaryOperator operation;
        std::int64_t left;
        std::int64_t right;
        std::int64_t expected;
    };

    const auto scenarios = std::array {
        Scenario {BuiltinType::I8, BinaryOperator::Add, 127, 1, -128},
        Scenario {BuiltinType::I8, BinaryOperator::Subtract, -128, 1, 127},
        Scenario {BuiltinType::I8, BinaryOperator::Multiply, 64, 2, -128},
        Scenario {BuiltinType::I8, BinaryOperator::LeftShift, -1, 1, -2},
        Scenario {BuiltinType::I8, BinaryOperator::Divide, -128, -1, -128},
        Scenario {BuiltinType::I8, BinaryOperator::Remainder, -128, -1, 0},
        Scenario {BuiltinType::U8, BinaryOperator::Add, 255, 1, 0},
        Scenario {BuiltinType::U8, BinaryOperator::Subtract, 0, 1, 255},
        Scenario {BuiltinType::U8, BinaryOperator::Multiply, 128, 2, 0},
        Scenario {BuiltinType::U8, BinaryOperator::LeftShift, 128, 1, 0},
        Scenario {
            BuiltinType::I64,
            BinaryOperator::Add,
            std::numeric_limits<std::int64_t>::max(),
            1,
            std::numeric_limits<std::int64_t>::min()
        },
        Scenario {
            BuiltinType::I64,
            BinaryOperator::Divide,
            std::numeric_limits<std::int64_t>::min(),
            -1,
            std::numeric_limits<std::int64_t>::min()
        },
    };
    for (const auto& scenario : scenarios) {
        const auto type = values.intern_builtin_type(scenario.type);
        const auto result = evaluate_binary_constant_value(
            values,
            scenario.operation,
            constant_test_integer_fact(type, scenario.left),
            constant_test_integer_fact(type, scenario.right),
            type,
            IntegerArithmetic::Wrapping
        );
        REQUIRE(result.has_value());
        CHECK(
            std::get<IntegerConstant>(result->value)
            == IntegerConstant::from_signed(scenario.expected)
        );
    }
    const auto i8 = values.intern_builtin_type(BuiltinType::I8);
    const auto minimum = constant_test_integer_fact(i8, -128);
    const auto negated = evaluate_unary_constant_value(
        values,
        UnaryOperator::Negate,
        minimum,
        i8,
        IntegerArithmetic::Wrapping
    );
    REQUIRE(negated.has_value());
    CHECK(std::get<IntegerConstant>(negated->value) == IntegerConstant::from_signed(-128));
    const auto checked = evaluate_unary_constant_value(values, UnaryOperator::Negate, minimum, i8);
    REQUIRE_FALSE(checked.has_value());
    CHECK(checked.error() == ConstantEvaluationFailure::IntegerOverflow);
    for (const auto shift : {-1, 8}) {
        const auto invalid = evaluate_binary_constant_value(
            values,
            BinaryOperator::LeftShift,
            minimum,
            constant_test_integer_fact(i8, shift),
            i8,
            IntegerArithmetic::Wrapping
        );
        REQUIRE_FALSE(invalid.has_value());
        CHECK(invalid.error() == ConstantEvaluationFailure::ShiftOutOfRange);
    }
    const auto zero = evaluate_binary_constant_value(
        values,
        BinaryOperator::Divide,
        minimum,
        constant_test_integer_fact(i8, 0),
        i8,
        IntegerArithmetic::Wrapping
    );
    REQUIRE_FALSE(zero.has_value());
    CHECK(zero.error() == ConstantEvaluationFailure::DivideByZero);
}
