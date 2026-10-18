module carven:support.process;

import std;

namespace process {

// Process-entry adapter: pass main's argc/argv, not a synthetic argument list.
// Windows reads the CRT's wide arguments; POSIX requires UTF-8 bytes.
// Returns owned UTF-8 strings, including the executable name; invalid text is an error.
auto arguments(int argc, const char* const* argv) noexcept
    -> std::expected<std::vector<std::string>, std::string_view>;

// Reads a null-terminated UTF-8 variable name from the current process environment.
// Owns the UTF-8 value, preserves empty values, and returns nullopt when absent.
// Invalid host text is an error.
auto environment(const char* name) noexcept
    -> std::expected<std::optional<std::string>, std::string_view>;

// Enables byte-preserving standard input and output for the current process.
auto enable_binary_standard_streams() noexcept -> bool;

} // namespace process
