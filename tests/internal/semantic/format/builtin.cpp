module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.format.builtin;

import :semantic.format.builtin;
import :semantic.semir.constant;
import :semantic.semir.format;
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

TEST_CASE("Builtin formatting: floating policies match native output and bound work") {
    const auto check = []<typename Float>(Float value) static noexcept {
        const auto constant = [&]() noexcept -> BuiltinFormatValue {
            if constexpr (std::same_as<Float, float>) {
                return F32Constant {.value = value};
            } else {
                return F64Constant {.value = value};
            }
        }();
        for (const auto specification :
             {"",
              "f",
              ".0f",
              ".2f",
              ".17g",
              ".4e",
              "a",
              ".3A",
              "+012.2f",
              "#g",
              "*>12.3G",
              "我^12.2f",
              " .2",
              ".0g"}) {
            CAPTURE(value);
            CAPTURE(specification);
            const auto expected =
                std::vformat(std::format("{{:{}}}", specification), std::make_format_args(value));
            const auto actual = format_builtin_value(constant, specification, 2048uz);
            REQUIRE(actual.has_value());
            CHECK(*actual == expected);
            const auto limited =
                format_builtin_value(constant, specification, expected.size() - 1uz);
            REQUIRE_FALSE(limited.has_value());
            CHECK(limited.error() == BuiltinFormatFailureKind::Limit);
        }
    };
    for (const auto value :
         {0.0,
          -0.0,
          1.25,
          -42.5,
          1.0e20,
          1.0e-12,
          std::numeric_limits<double>::max(),
          std::numeric_limits<double>::denorm_min(),
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()}) {
        check(value);
    }
    for (const auto value :
         {0.0f,
          -0.0f,
          1.25f,
          std::numeric_limits<float>::max(),
          std::numeric_limits<float>::denorm_min(),
          std::numeric_limits<float>::infinity()}) {
        check(value);
    }
    for (const auto invalid : {"x", "00", ".", "..2f", "2.3.4f", "{}", ".{}f", "{>5", "L", "+-f"}) {
        CAPTURE(invalid);
        const auto result = format_builtin_value(F64Constant {.value = 1.0}, invalid, 1024uz);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == BuiltinFormatFailureKind::Unsupported);
    }
    for (const auto oversized :
         {"999999999999999999999999f", ".99999999999999999999999f", ".2048f"}) {
        const auto result = format_builtin_value(F64Constant {.value = 1.0}, oversized, 1024uz);
        REQUIRE_FALSE(result.has_value());
        CHECK(result.error() == BuiltinFormatFailureKind::Limit);
    }
}

TEST_CASE("Builtin formatting: dynamic integer widths preserve zero and budget boundaries") {
    const auto specification = FormatSpec {
        .parts = {{FormatHole {
            .operand_index = 0uz,
            .has_specification = true,
            .specification = {
                {FormatHole {.operand_index = 1uz, .has_specification = false, .specification = {}}}
            }
        }}}
    };
    for (const auto width : {0, 1, 4}) {
        CAPTURE(width);
        const auto arguments = std::array<BuiltinFormatValue, 2> {
            IntegerConstant::from_signed(1),
            IntegerConstant::from_signed(width)
        };
        const auto expected = std::string(width > 1 ? width - 1 : 0, ' ') + "1";
        const auto result = format_builtin(specification, arguments, expected.size());
        REQUIRE(result.has_value());
        CHECK(*result == expected);
        const auto limited = format_builtin(specification, arguments, expected.size() - 1uz);
        REQUIRE_FALSE(limited.has_value());
        CHECK(limited.error().kind == BuiltinFormatFailureKind::Limit);
    }
    const auto arguments = std::array<BuiltinFormatValue, 2> {
        BooleanConstant {.value = true},
        IntegerConstant::from_signed(0)
    };
    const auto unsupported = format_builtin(specification, arguments, 16uz);
    REQUIRE_FALSE(unsupported.has_value());
    CHECK(unsupported.error().kind == BuiltinFormatFailureKind::Unsupported);
}
