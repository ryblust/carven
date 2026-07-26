module carven:support.invariant;

import std;

[[noreturn]] auto invariant_violation(
    std::string_view message,
    std::source_location location = std::source_location::current()
) noexcept -> void;

[[noreturn]] auto resource_limit_exceeded(
    std::string_view message,
    std::source_location location = std::source_location::current()
) noexcept -> void;
