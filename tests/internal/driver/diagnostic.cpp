module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.driver.diagnostic;

import :driver.diagnostic;
import std;

TEST_CASE("Diagnostic presentation: command errors retain text with or without styling") {
    const auto plain = render_driver_error(
        "unknown check option '--bad'",
        false,
        "carven check",
        "choose a supported option"
    );
    const auto styled = render_driver_error(
        "unknown check option '--bad'",
        true,
        "carven check",
        "choose a supported option"
    );

    CHECK_EQ(
        plain,
        "carven: error: unknown check option '--bad'\n"
        "  help: choose a supported option\n"
        "Run 'carven check --help' for usage.\n"
    );
    // Only complete, non-nested style/reset pairs may be removed.
    const auto styled_text = std::regex("\\x1b\\[[0-9;]+m([^\\x1b]*)\\x1b\\[0m");
    CHECK_NE(styled, plain);
    CHECK_EQ(std::regex_replace(styled, styled_text, "$1"), plain);
}
