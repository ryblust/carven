module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.format.builtin;

import :semantic.format.builtin;
import :semantic.semir.constant;
import std;

TEST_CASE("Builtin formatting: integer bytes agree with native standard formatting") {
    const auto numbers = std::to_array<std::int64_t>({
        0,
        1,
        -1,
        42,
        -42,
        std::numeric_limits<std::int64_t>::min(),
        std::numeric_limits<std::int64_t>::max(),
    });
    const auto specifications = std::to_array<std::string_view>({
        "",
        "d",
        "0",
        "04",
        "24",
        "024",
        "b",
        "B",
        "o",
        "x",
        "X",
        "080b",
        "024X",
    });
    for (auto value : numbers) {
        const auto constant = IntegerConstant::from_signed(value);
        for (const auto specification : specifications) {
            CAPTURE(value);
            CAPTURE(specification);
            const auto actual = format_builtin_value(constant, specification, 1024uz);
            REQUIRE(actual.has_value());
            const auto format = std::format("{{:{}}}", specification);
            CHECK(*actual == std::vformat(format, std::make_format_args(value)));
        }
    }
    auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto constant = IntegerConstant::from_parts(maximum, false);
    for (const auto specification : specifications) {
        const auto actual = format_builtin_value(constant, specification, 1024uz);
        REQUIRE(actual.has_value());
        const auto format = std::format("{{:{}}}", specification);
        CHECK(*actual == std::vformat(format, std::make_format_args(maximum)));
    }
}

TEST_CASE(
    "Builtin formatting: supported values respect byte budgets and unsupported inputs decline"
) {
    struct Scenario final {
        BuiltinFormatValue value;
        std::string_view text;
    };

    const auto embedded = std::string_view("a\0我", 5uz);
    const auto scenarios = std::array {
        Scenario {BooleanConstant {.value = false}, "false"},
        Scenario {CharacterConstant {.scalar = U'我'}, "我"},
        Scenario {embedded, embedded},
        Scenario {std::string_view {}, ""},
    };
    for (auto index = 0uz; index < scenarios.size(); ++index) {
        CAPTURE(index);
        const auto& scenario = scenarios[index];
        const auto formatted = format_builtin_value(scenario.value, {}, scenario.text.size());
        REQUIRE(formatted.has_value());
        CHECK(*formatted == scenario.text);
        if (!scenario.text.empty()) {
            const auto limited =
                format_builtin_value(scenario.value, {}, scenario.text.size() - 1uz);
            REQUIRE_FALSE(limited.has_value());
            CHECK(limited.error() == BuiltinFormatFailureKind::Limit);
        }
        const auto unsupported = format_builtin_value(scenario.value, "x", 128uz);
        REQUIRE_FALSE(unsupported.has_value());
        CHECK(unsupported.error() == BuiltinFormatFailureKind::Unsupported);
    }
    const auto unavailable = format_builtin_value(BuiltinFormatValue {}, {}, 128uz);
    REQUIRE_FALSE(unavailable.has_value());
    CHECK(unavailable.error() == BuiltinFormatFailureKind::Unsupported);
}
