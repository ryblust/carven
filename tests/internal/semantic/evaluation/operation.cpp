module carven:test.internal.semantic.evaluation.operation;

import :diagnostics.code;
import :semantic.evaluation.operation;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.constant_access;
import :semantic.semir.operation;
import :semantic.semir.type;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Semantic constant evaluation: integer folds use the runtime arithmetic contract",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto boolean = values.builtin_type(BuiltinType::Bool);
            const auto i8 = values.builtin_type(BuiltinType::I8);
            const auto u8 = values.builtin_type(BuiltinType::U8);

            const auto maximum = values.intern_constant(constant_test_integer_fact(i8, 127));
            const auto one_i8 = values.intern_constant(constant_test_integer_fact(i8, 1));
            const auto wrapped =
                fold_binary_constant(values, BinaryOperator::Add, maximum, one_i8, i8);
            if (!ct::expect(wrapped.has_value())) {
                return;
            }
            ct::expect(
                std::get<IntegerConstant>(wrapped->value) == IntegerConstant::from_signed(-128)
            );

            const auto zero_i8 = values.intern_constant(constant_test_integer_fact(i8, 0));
            const auto divide_by_zero =
                fold_binary_constant(values, BinaryOperator::Divide, one_i8, zero_i8, i8);
            if (!ct::expect(!(divide_by_zero.has_value()))) {
                return;
            }
            ct::expect_equal(divide_by_zero.error(), ConstantEvaluationFailure::DivideByZero);
            if (!ct::expect(constant_evaluation_diagnostic(divide_by_zero.error()).has_value())) {
                return;
            }
            ct::expect_equal(
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
            if (!ct::expect(!(bad_shift.has_value()))) {
                return;
            }
            ct::expect_equal(bad_shift.error(), ConstantEvaluationFailure::ShiftOutOfRange);

            const auto equal =
                fold_binary_constant(values, BinaryOperator::Equal, one_i8, one_i8, boolean);
            if (!ct::expect(equal.has_value())) {
                return;
            }
            ct::expect(std::get<BooleanConstant>(equal->value).value);

            const auto absent =
                fold_unary_constant(values, UnaryOperator::Negate, std::nullopt, i8);
            if (!ct::expect(!(absent.has_value()))) {
                return;
            }
            ct::expect_equal(absent.error(), ConstantEvaluationFailure::OperandNotConstant);
            ct::expect(!(constant_evaluation_diagnostic(absent.error()).has_value()));
        }
    );

    ct::test(
        "Semantic constant evaluation: casts and text intrinsics return canonical facts",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto i8 = values.builtin_type(BuiltinType::I8);
            const auto u8 = values.builtin_type(BuiltinType::U8);
            const auto usize = values.builtin_type(BuiltinType::Usize);
            const auto boolean = values.builtin_type(BuiltinType::Bool);
            const auto f32 = values.builtin_type(BuiltinType::F32);
            const auto f64 = values.builtin_type(BuiltinType::F64);

            const auto negative_one = values.intern_constant(constant_test_integer_fact(i8, -1));
            const auto wrapped =
                fold_cast_constant(values, CastKind::IntegerToInteger, negative_one, u8);
            if (!ct::expect(wrapped.has_value())) {
                return;
            }
            const auto wrapped_value = std::get<IntegerConstant>(wrapped->value).as_unsigned();
            if (!ct::expect(wrapped_value.has_value())) {
                return;
            }
            ct::expect_equal(*wrapped_value, 255u);

            const auto narrow_float = values.intern_constant(
                ConstantFact {
                    .type = f32,
                    .value = F32Constant {.value = 1.5f},
                }
            );
            const auto widened =
                fold_cast_constant(values, CastKind::FloatingWiden, narrow_float, f64);
            if (!ct::expect(widened.has_value())) {
                return;
            }
            ct::expect_equal(std::get<F64Constant>(widened->value).value, 1.5);

            const auto string_id = values.intern_constant(
                ConstantFact {
                    .type = values.builtin_type(BuiltinType::Str),
                    .value = StringConstant {.value = values.intern_spelling("abc")},
                }
            );
            const auto string = load_constant_fact(values, string_id);
            if (!ct::expect(string.has_value())) {
                return;
            }
            const auto length =
                fold_text_intrinsic_constant(values, TextIntrinsic::Len, string_id, usize);
            if (!ct::expect(length.has_value())) {
                return;
            }
            const auto length_value = std::get<IntegerConstant>(length->value).as_unsigned();
            if (!ct::expect(length_value.has_value())) {
                return;
            }
            ct::expect_equal(*length_value, 3u);
            const auto empty =
                fold_text_intrinsic_constant(values, TextIntrinsic::IsEmpty, string_id, boolean);
            if (!ct::expect(empty.has_value())) {
                return;
            }
            ct::expect(!(std::get<BooleanConstant>(empty->value).value));
            const auto view = fold_text_intrinsic_constant(
                values,
                TextIntrinsic::Bytes,
                string_id,
                (*string)->type
            );
            if (!ct::expect(!(view.has_value()))) {
                return;
            }
            ct::expect_equal(view.error(), ConstantEvaluationFailure::UnsupportedOperation);
        }
    );

    ct::test(
        "Semantic constant evaluation: operand facts retain program owner evidence",
        [] static noexcept {
            const auto first = ConstantEvaluationFixture();
            const auto second = ConstantEvaluationFixture();
            const auto& first_values = first.compilation;
            const auto& second_values = second.compilation;
            const auto first_i32 = first_values.builtin_type(BuiltinType::I32);
            const auto second_i32 = second_values.builtin_type(BuiltinType::I32);
            const auto foreign = constant_test_integer_fact(first_i32, 1);
            ct::expect(expect_termination("semantic-constant-foreign-owner", [&] noexcept {
                static_cast<void>(evaluate_unary_constant_value(
                    second_values,
                    UnaryOperator::Negate,
                    foreign,
                    second_i32
                ));
            }));
        }
    );

    ct::test(
        "Semantic constants: floating identity preserves signed zero while equality compares values",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto check_zero = [&]<typename Floating>(BuiltinType builtin) noexcept {
                const auto type = values.builtin_type(builtin);
                const auto positive = ConstantFact {.type = type, .value = Floating {.value = 0.0}};
                const auto negative =
                    ConstantFact {.type = type, .value = Floating {.value = -0.0}};
                const auto positive_id = values.intern_constant(positive);
                const auto negative_id = values.intern_constant(negative);
                ct::expect(positive_id != negative_id)
                    .note("positive and negative constant IDs differ");
                ct::expect(((values.intern_constant(negative)) == (negative_id)))
                    .note("values.intern_constant(negative) == negative_id");
                ct::expect(constant_value_equal(values, positive.value, negative.value));
                ct::expect(
                    std::signbit(std::get<Floating>(values.constant(negative_id).value).value)
                );
            };
            check_zero.operator()<F32Constant>(BuiltinType::F32);
            check_zero.operator()<F64Constant>(BuiltinType::F64);
        }
    );

    ct::test(
        "Semantic execution: runtime integer arithmetic wraps at the operand width",
        [] static noexcept {
            const auto fixture = ConstantEvaluationFixture();
            const auto& values = fixture.compilation;

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
            ct::each(
                scenarios,
                [](const auto& scenario) static noexcept {
                    return std::format(
                        "{} {} {} {}",
                        std::to_underlying(scenario.type),
                        std::to_underlying(scenario.operation),
                        scenario.left,
                        scenario.right
                    );
                },
                [&](const auto& scenario) noexcept {
                    const auto type = values.builtin_type(scenario.type);
                    const auto result = evaluate_binary_constant_value(
                        values,
                        scenario.operation,
                        constant_test_integer_fact(type, scenario.left),
                        constant_test_integer_fact(type, scenario.right),
                        type,
                        IntegerArithmetic::Wrapping
                    );
                    if (!ct::expect(result.has_value())) {
                        return;
                    }
                    ct::expect(
                        std::get<IntegerConstant>(result->value)
                        == IntegerConstant::from_signed(scenario.expected)
                    );
                }
            );
            const auto i8 = values.builtin_type(BuiltinType::I8);
            const auto minimum = constant_test_integer_fact(i8, -128);
            const auto negated = evaluate_unary_constant_value(
                values,
                UnaryOperator::Negate,
                minimum,
                i8,
                IntegerArithmetic::Wrapping
            );
            if (!ct::expect(negated.has_value())) {
                return;
            }
            ct::expect(
                std::get<IntegerConstant>(negated->value) == IntegerConstant::from_signed(-128)
            );
            const auto checked = evaluate_unary_constant_value(
                values,
                UnaryOperator::Negate,
                minimum,
                i8,
                IntegerArithmetic::Checked
            );
            if (!ct::expect(!(checked.has_value()))) {
                return;
            }
            ct::expect(checked.error() == ConstantEvaluationFailure::IntegerOverflow);
            for (const auto shift : {-1, 8}) {
                const auto invalid = evaluate_binary_constant_value(
                    values,
                    BinaryOperator::LeftShift,
                    minimum,
                    constant_test_integer_fact(i8, shift),
                    i8,
                    IntegerArithmetic::Wrapping
                );
                if (!ct::expect(!(invalid.has_value()))) {
                    return;
                }
                ct::expect(invalid.error() == ConstantEvaluationFailure::ShiftOutOfRange);
            }
            const auto zero = evaluate_binary_constant_value(
                values,
                BinaryOperator::Divide,
                minimum,
                constant_test_integer_fact(i8, 0),
                i8,
                IntegerArithmetic::Wrapping
            );
            if (!ct::expect(!(zero.has_value()))) {
                return;
            }
            ct::expect(zero.error() == ConstantEvaluationFailure::DivideByZero);
        }
    );

    ct::test(
        "Semantic execution: floating operations preserve native values without runtime folding",
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& values = fixture.compilation;
            const auto boolean = values.builtin_type(BuiltinType::Bool);
            const auto exercise = [&]<typename Floating>(BuiltinType builtin) noexcept {
                const auto type = values.builtin_type(builtin);
                const auto left = ConstantFact {.type = type, .value = Floating {.value = 6.0}};
                const auto right = ConstantFact {.type = type, .value = Floating {.value = 2.0}};
                struct Case final {
                    BinaryOperator operation;
                    double expected;
                };
                const auto cases = std::array {
                    Case {.operation = BinaryOperator::Add, .expected = 8.0},
                    Case {.operation = BinaryOperator::Subtract, .expected = 4.0},
                    Case {.operation = BinaryOperator::Multiply, .expected = 12.0},
                    Case {.operation = BinaryOperator::Divide, .expected = 3.0},
                };
                ct::each(
                    cases,
                    [&](const Case& item) noexcept {
                        return std::format(
                            "type {} operation {}",
                            std::to_underlying(builtin),
                            std::to_underlying(item.operation)
                        );
                    },
                    [&](const Case& item) noexcept {
                        const auto result = evaluate_binary_constant_value(
                            values,
                            item.operation,
                            left,
                            right,
                            type
                        );
                        if (!ct::expect(result.has_value())) {
                            return;
                        }
                        ct::expect(std::get<Floating>(result->value).value == item.expected);
                        ct::expect(!(fold_binary_constant(
                                         values,
                                         item.operation,
                                         values.intern_constant(left),
                                         values.intern_constant(right),
                                         type
                        )
                                         .has_value()));
                    }
                );
                const auto zero = ConstantFact {.type = type, .value = Floating {.value = 0.0}};
                const auto negative =
                    evaluate_unary_constant_value(values, UnaryOperator::Negate, zero, type);
                if (!ct::expect(negative.has_value())) {
                    return;
                }
                ct::expect(std::signbit(std::get<Floating>(negative->value).value));
                ct::expect(!(fold_unary_constant(
                                 values,
                                 UnaryOperator::Negate,
                                 values.intern_constant(zero),
                                 type
                )
                                 .has_value()));
                const auto infinity = evaluate_binary_constant_value(
                    values,
                    BinaryOperator::Divide,
                    left,
                    zero,
                    type
                );
                if (!ct::expect(infinity.has_value())) {
                    return;
                }
                ct::expect(std::isinf(std::get<Floating>(infinity->value).value));
                const auto nan = evaluate_binary_constant_value(
                    values,
                    BinaryOperator::Divide,
                    zero,
                    zero,
                    type
                );
                if (!ct::expect(nan.has_value())) {
                    return;
                }
                ct::expect(std::isnan(std::get<Floating>(nan->value).value));
                const auto comparisons = std::array {
                    BinaryOperator::Equal,
                    BinaryOperator::Less,
                    BinaryOperator::LessEqual,
                    BinaryOperator::Greater,
                    BinaryOperator::GreaterEqual
                };
                for (const auto operation : comparisons) {
                    const auto result =
                        evaluate_binary_constant_value(values, operation, *nan, right, boolean);
                    if (!ct::expect(result.has_value())) {
                        return;
                    }
                    ct::expect(!(std::get<BooleanConstant>(result->value).value));
                }
                const auto unequal = evaluate_binary_constant_value(
                    values,
                    BinaryOperator::NotEqual,
                    *nan,
                    *nan,
                    boolean
                );
                if (!ct::expect(unequal.has_value())) {
                    return;
                }
                ct::expect(std::get<BooleanConstant>(unequal->value).value);
            };
            exercise.operator()<F32Constant>(BuiltinType::F32);
            exercise.operator()<F64Constant>(BuiltinType::F64);
        }
    );
});

} // namespace
