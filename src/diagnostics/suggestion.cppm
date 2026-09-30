module carven:diagnostics.suggestion;

import std;

// Returns "; did you mean 'x'?" for the closest candidate spelling, or empty
// text when no candidate is near enough to be the intended name.
auto spelling_suggestion(std::string_view name, std::span<const std::string> candidates) noexcept
    -> std::string;
