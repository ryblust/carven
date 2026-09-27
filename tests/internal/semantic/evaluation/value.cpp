module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.evaluation.value;

import :semantic.evaluation.value;
import :semantic.semir.delegation;
import :semantic.semir.type;
import :test.internal.semantic.evaluation.fixture;
import std;

static_assert(!std::is_copy_constructible_v<ExecutionValue>);
static_assert(!std::is_copy_assignable_v<ExecutionValue>);
static_assert(!std::is_copy_constructible_v<ExecutionOwnedText>);

TEST_CASE(
    "Constant values: text observation and equality are independent of storage representation"
) {
    auto fixture = ConstantEvaluationFixture();
    const auto& values = fixture.compilation;
    const auto bytes = std::string("a\0我", 5uz);
    const auto fact = ConstantFact {
        .type = values.builtin_type(BuiltinType::Str),
        .value = StringConstant {.value = fixture.compilation.intern_spelling(bytes)}
    };
    const auto retained = fixture.compilation.intern_constant(fact);
    const auto representations = std::array<ExecutionValue, 4> {
        retained,
        *constant_atom(fact),
        ExecutionText(bytes),
        ExecutionOwnedText(bytes)
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
            .type = values.builtin_type(BuiltinType::I32),
            .value = IntegerConstant::from_signed(1)
        }
    );
    CHECK_FALSE(execution_text(values, nontext).has_value());
    auto steps = 0uz;
    CHECK(execution_equal(values, nontext, retained, steps, 1uz) == false);
}

TEST_CASE("Constant values: comparison distinguishes expired text, unsupported values and limits") {
    auto fixture = ConstantEvaluationFixture();
    const auto& values = fixture.compilation;
    auto storage = std::optional(ExecutionText(std::string("text")));
    const auto borrowed = ExecutionValue(storage->borrow());
    const auto owned = ExecutionValue(ExecutionOwnedText("text"));
    auto steps = 0uz;
    CHECK(execution_equal(values, borrowed, owned, steps, 1uz) == true);
    const auto exhausted = execution_equal(values, borrowed, owned, steps, 1uz);
    REQUIRE_FALSE(exhausted.has_value());
    CHECK(exhausted.error() == ExecutionComparisonFailure::StepLimit);

    storage.reset();
    for (const auto reverse : {false, true}) {
        CAPTURE(reverse);
        steps = 0uz;
        const auto result = execution_equal(
            values,
            reverse ? owned : borrowed,
            reverse ? borrowed : owned,
            steps,
            1uz
        );
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == ExecutionComparisonFailure::ExpiredText);
    }

    const auto cstring = ExecutionValue(
        ConstantAtom {
            .type = fixture.compilation.intern_type(
                {.value = CppTypeValue {.form = CppConstCharPointerType {}}}
            ),
            .value = CStringConstant {.value = fixture.compilation.intern_spelling("text")}
        }
    );
    steps = 0uz;
    const auto unsupported = execution_equal(values, cstring, cstring, steps, 1uz);
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error() == ExecutionComparisonFailure::Unsupported);
}

TEST_CASE("Execution text: shared content and String borrows have separate lifetimes") {
    auto owner = ExecutionOwnedText("a");
    const auto borrowed = owner.borrow();
    const auto copied_borrow = borrowed;
    REQUIRE(borrowed.bytes() == "a");
    auto moved_owner = std::move(owner);
    CHECK(copied_borrow.bytes() == "a");
    moved_owner.append("b");
    CHECK(moved_owner.bytes() == "ab");
    CHECK_FALSE(borrowed.bytes().has_value());
    CHECK_FALSE(copied_borrow.bytes().has_value());

    const auto before_take = moved_owner.borrow();
    moved_owner.transfer();
    CHECK(moved_owner.bytes() == "ab");
    CHECK_FALSE(before_take.bytes().has_value());

    auto text = std::optional(ExecutionText(std::string("shared")));
    auto shared = std::optional(*text);
    const auto view = text->borrow();
    text.reset();
    CHECK(shared->bytes() == "shared");
    CHECK(view.bytes() == "shared");
    shared.reset();
    CHECK_FALSE(view.bytes().has_value());
}
