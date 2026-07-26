module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.driver.input_path;

import :driver.input_path;
import std;

TEST_CASE("Input path: normalized relative paths preserve their module hierarchy") {
    const auto direct = derive_input_module_path("src/app/main.cv");
    const auto normalized = derive_input_module_path("src/app/./nested/../main.cv");

    REQUIRE(direct.has_value());
    REQUIRE(normalized.has_value());
    CHECK_EQ(direct->value(), "src.app.main");
    CHECK_EQ(*normalized, *direct);
}

TEST_CASE("Input path: identifier-only hierarchy remains exact") {
    const auto upper = derive_input_module_path("Case.cv");
    const auto lower = derive_input_module_path("case.cv");
    const auto nested = derive_input_module_path("linear_algebra/vector2.cv");
    const auto cv = derive_input_module_path("cv.cv");
    const auto double_underscore = derive_input_module_path("__carven_internal/__value.cv");

    REQUIRE(upper.has_value());
    REQUIRE(lower.has_value());
    REQUIRE(nested.has_value());
    REQUIRE(cv.has_value());
    REQUIRE(double_underscore.has_value());
    CHECK_EQ(upper->value(), "Case");
    CHECK_EQ(lower->value(), "case");
    CHECK_NE(*upper, *lower);
    CHECK_EQ(nested->value(), "linear_algebra.vector2");
    CHECK_EQ(cv->value(), "cv");
    CHECK_EQ(double_underscore->value(), "__carven_internal.__value");
}

TEST_CASE("Input path: the standard craft domain is reserved for toolchain source") {
    static constexpr auto reserved = std::to_array<std::string_view>({
        "crafts/std/io.cv",
        "crafts/vendor/../std/nested/value.cv",
    });
    for (const auto input : reserved) {
        const auto result = derive_input_module_path(input);
        CAPTURE(input);
        REQUIRE_FALSE(result.has_value());
        CHECK_EQ(
            result.error(),
            std::format(
                "input '{}' resolves to the toolchain-reserved 'crafts.std' module domain",
                input
            )
        );
    }

    const auto unprefixed = derive_input_module_path("std/io.cv");
    const auto neighboring_craft = derive_input_module_path("crafts/stdx/io.cv");
    REQUIRE(unprefixed.has_value());
    REQUIRE(neighboring_craft.has_value());
    CHECK_EQ(unprefixed->value(), "std.io");
    CHECK_EQ(neighboring_craft->value(), "crafts.stdx.io");
}

TEST_CASE("Input path: invalid module names identify the first offending component") {
    const auto cases = std::array {
        std::pair {
            std::string_view("bad-dir/main.cv"),
            std::string_view(
                "input 'bad-dir/main.cv' has invalid module directory component 'bad-dir'"
            ),
        },
        std::pair {
            std::string_view("math/bad-name.cv"),
            std::string_view("input 'math/bad-name.cv' has invalid module file stem 'bad-name'"),
        },
        std::pair {
            std::string_view("match/value.cv"),
            std::string_view(
                "input 'match/value.cv' uses language keyword 'match' as a module directory "
                "component"
            ),
        },
        std::pair {
            std::string_view("math/match.cv"),
            std::string_view(
                "input 'math/match.cv' uses language keyword 'match' as a module file stem"
            ),
        },
        std::pair {
            std::string_view("42/value.cv"),
            std::string_view("input '42/value.cv' has invalid module directory component '42'"),
        },
        std::pair {
            std::string_view("math/42.cv"),
            std::string_view("input 'math/42.cv' has invalid module file stem '42'"),
        },
        std::pair {
            std::string_view("\xe6\x95\xb0\xe5\xad\xa6/vector.cv"),
            std::string_view(
                "input '\xe6\x95\xb0\xe5\xad\xa6/vector.cv' has invalid module directory "
                "component '\xe6\x95\xb0\xe5\xad\xa6'"
            ),
        },
    };

    for (const auto& [input, error] : cases) {
        const auto result = derive_input_module_path(input);
        CAPTURE(input);
        REQUIRE(!result.has_value());
        CHECK_EQ(result.error(), error);
    }
}

TEST_CASE("Input path: invalid paths are rejected before source acquisition") {
    const auto cases = std::array {
        std::pair {
            std::string_view(""),
            std::string_view("input path cannot be empty"),
        },
        std::pair {
            std::string_view("/absolute.cv"),
            std::string_view("input '/absolute.cv' must be relative"),
        },
        std::pair {
            std::string_view("../outside.cv"),
            std::string_view("input '../outside.cv' escapes the working directory"),
        },
        std::pair {
            std::string_view("wrong.txt"),
            std::string_view("input 'wrong.txt' does not have a .cv extension"),
        },
        std::pair {
            std::string_view("..cv"),
            std::string_view("cannot derive a module path from '..cv'"),
        },
        std::pair {
            std::string_view("a/..cv"),
            std::string_view("input 'a/..cv' derives an empty module path component"),
        },
        std::pair {
            std::string_view("bad\\name.cv"),
            std::string_view("input path must use '/' as the path separator"),
        },
        std::pair {
            std::string_view("quote\"name.cv"),
            std::string_view("input path cannot contain '\"'"),
        },
        std::pair {
            std::string_view("line\nname.cv"),
            std::string_view("input path cannot contain a line break"),
        },
        std::pair {
            std::string_view("carriage\rname.cv"),
            std::string_view("input path cannot contain a line break"),
        },
    };

    for (const auto& [input, error] : cases) {
        const auto result = derive_input_module_path(input);
        CAPTURE(input);
        REQUIRE(!result.has_value());
        CHECK_EQ(result.error(), error);
    }

    const auto with_nul = std::string_view("nul\0name.cv", 11);
    const auto nul_result = derive_input_module_path(with_nul);
    REQUIRE(!nul_result.has_value());
    CHECK_EQ(nul_result.error(), "input path cannot contain NUL");

    auto invalid_utf8 = std::string("invalid-");
    invalid_utf8.push_back(static_cast<char>(0xff));
    invalid_utf8 += ".cv";
    const auto utf8_result = derive_input_module_path(invalid_utf8);
    REQUIRE(!utf8_result.has_value());
    CHECK_EQ(utf8_result.error(), "input path is not valid UTF-8");
}

#if defined(_WIN32)
TEST_CASE("Input path: drive-relative paths cannot escape the working directory") {
    const auto result = derive_input_module_path("C:escape.cv");

    REQUIRE(!result.has_value());
    CHECK_EQ(result.error(), "input 'C:escape.cv' must be relative");
}
#endif
