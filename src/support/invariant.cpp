module carven:support.invariant.impl;

import :support.invariant;
import std;

namespace {

[[noreturn]] auto fail(
    std::string_view category,
    std::string_view message,
    std::source_location location
) noexcept -> void {
    std::println(std::cerr, "carven: {}: {}", category, message);
    std::println(
        std::cerr,
        "  at {}:{}:{}",
        location.file_name(),
        location.line(),
        location.column()
    );
    std::abort();
}

} // namespace

auto invariant_violation(std::string_view message, std::source_location location) noexcept -> void {
    fail("invariant violation", message, location);
}

auto resource_limit_exceeded(std::string_view message, std::source_location location) noexcept
    -> void {
    fail("resource limit exceeded", message, location);
}
