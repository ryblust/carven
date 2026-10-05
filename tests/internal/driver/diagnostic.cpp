module carven:test.internal.driver.diagnostic;

import :driver.diagnostic;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Diagnostic presentation: command errors retain text with or without styling"_test =
        [] static noexcept {
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

            expect_equal(
                plain,
                std::string_view(
                    "carven: error: unknown check option '--bad'\n"
                    "  help: choose a supported option\n"
                    "Run 'carven check --help' for usage.\n"
                )
            );
            // Only complete, non-nested style/reset pairs may be removed.
            const auto styled_text = std::regex("\\x1b\\[[0-9;]+m([^\\x1b]*)\\x1b\\[0m");
            expect_not_equal(styled, plain);
            expect_equal(std::regex_replace(styled, styled_text, "$1"), plain);
        };
});

} // namespace
