module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.evaluation.value;

import :semantic.evaluation.value;
import :test.internal.semantic.evaluation.fixture;
import std;

TEST_CASE(
    "Constant values: text observation and equality are independent of storage representation"
) {
    auto fixture = ConstantEvaluationFixture();
    auto& values = fixture.compilation;
    const auto bytes = std::string("a\0我", 5uz);
    const auto fact = ConstantFact {
        .type = values.intern_builtin_type(BuiltinType::Str),
        .value = StringConstant {.value = values.intern_spelling(bytes)}
    };
    const auto retained = values.intern_constant(fact);
    const auto representations = std::array<ExecutionValue, 4> {
        retained,
        *constant_atom(fact),
        ExecutionText {.bytes = std::make_shared<const std::string>(bytes)},
        ExecutionOwnedText {.bytes = bytes}
    };
    for (auto index = 0uz; index < representations.size(); ++index) {
        CAPTURE(index);
        const auto text = execution_text(values, representations[index]);
        REQUIRE(text.has_value());
        CHECK(*text == bytes);
        auto steps = 0uz;
        CHECK(execution_equal(values, representations[index], retained, steps, 1uz) == true);
        CHECK(steps == 1uz);
    }
    const auto nontext = ExecutionValue(
        ConstantAtom {
            .type = values.intern_builtin_type(BuiltinType::I32),
            .value = IntegerConstant::from_signed(1)
        }
    );
    CHECK_FALSE(execution_text(values, nontext).has_value());
    auto steps = 0uz;
    CHECK(execution_equal(values, nontext, retained, steps, 1uz) == false);
}
