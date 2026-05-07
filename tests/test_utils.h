#pragma once

#include <print>
#include <source_location>
#include <string_view>

static auto current_test = std::string_view();
static auto failed_count = 0;

[[maybe_unused]] static auto check(
    bool condition,
    std::string_view expr,
    std::source_location loc = std::source_location::current()
) noexcept -> void {
    if (!condition) {
        ++failed_count;
        if (!current_test.empty()) {
            std::println("FAIL {}: {} ({}:{})", current_test, expr, loc.file_name(), loc.line());
        } else {
            std::println("FAIL {} ({}:{})", expr, loc.file_name(), loc.line());
        }
    }
}

template<typename A, typename E>
[[maybe_unused]] static auto check_eq(
    A&& actual,
    E&& expected,
    std::string_view actual_expr,
    std::string_view expected_expr,
    std::source_location loc = std::source_location::current()
) noexcept -> void {
    if (!(actual == expected)) {
        ++failed_count;
        if (!current_test.empty()) {
            std::println("FAIL {} ({}:{})", current_test, loc.file_name(), loc.line());
        } else {
            std::println("FAIL ({}:{})", loc.file_name(), loc.line());
        }
        std::println("  {} = {}", actual_expr, actual);
        std::println("  {} = {}", expected_expr, expected);
    }
}

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

[[maybe_unused]] static auto print_test_result() noexcept -> int {
    if (failed_count == 0) {
        std::println("All tests passed.");
        return 0;
    }
    std::println("{} test(s) failed.", failed_count);
    return 1;
}
