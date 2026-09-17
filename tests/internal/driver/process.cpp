module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.driver.process;

import :driver.process;
import std;

TEST_CASE("Process: Windows arguments preserve empty values quotes and trailing backslashes") {
    struct Case final {
        std::string argument;
        std::string expected;
    };

    const auto cases = std::array {
        Case {.argument = "", .expected = "\"\""},
        Case {.argument = "two words", .expected = "\"two words\""},
        Case {.argument = "a\"b", .expected = "\"a\\\"b\""},
        Case {.argument = "a\\", .expected = "\"a\\\\\""},
        Case {.argument = "a\\\"b", .expected = "\"a\\\\\\\"b\""},
        Case {.argument = "; & %PATH%", .expected = "\"; & %PATH%\""},
    };
    for (const auto& item : cases) {
        CAPTURE(item.argument);
        const auto arguments = std::array {std::string("tool with spaces.exe"), item.argument};
        CHECK(windows_command_line(arguments) == "\"tool with spaces.exe\" " + item.expected);
    }
}

TEST_CASE("Process: missing executable and NUL arguments report launch errors") {
    CHECK_FALSE(run_process({}).has_value());
    CHECK_FALSE(run_process({"carven-test-missing-executable-5e5d3fd2"}).has_value());
    CHECK_FALSE(run_process({"unused", std::string("a\0b", 3uz)}).has_value());
}

TEST_CASE("Process: temporary run directories are distinct and owned by each request") {
    const auto first = create_run_directory();
    REQUIRE(first.has_value());
    const auto second = create_run_directory();
    REQUIRE(second.has_value());
    CHECK(*first != *second);
    CHECK(std::filesystem::is_directory(*first));
    CHECK(std::filesystem::is_directory(*second));
    auto error = std::error_code();
    CHECK(std::filesystem::remove(*first, error));
    CHECK_FALSE(error);
    CHECK(std::filesystem::remove(*second, error));
    CHECK_FALSE(error);
}
