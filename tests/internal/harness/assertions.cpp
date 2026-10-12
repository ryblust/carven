module carven:test.internal.harness.assertions;

import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Assertions: optional output distinguishes absent and nested values"_test = [] static noexcept {
        constexpr auto printed = [](const auto& value) static noexcept {
            auto output = std::ostringstream();
            write_test_value(output, value);
            return std::move(output).str();
        };
        expect_equal(printed(std::nullopt), "nullopt");
        expect_equal(printed(std::optional<int>()), "nullopt");
        expect_equal(printed(std::optional(0)), "optional(0)");
        expect_equal(
            printed(std::optional<std::optional<std::string>> {std::in_place, "hello"}),
            "optional(optional(\"hello\"))"
        );
        expect_equal(
            printed(std::optional<std::optional<int>> {std::in_place}),
            "optional(nullopt)"
        );
    };

    "Assertions: optional values and absence support comparison output"_test = [] static noexcept {
        expect_equal(std::optional(42), 42);
        expect_equal(std::optional<int>(), std::nullopt);
    };
});

} // namespace
