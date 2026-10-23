module carven:backend.emission.string;

import std;

auto cpp_string_token(std::string_view bytes) noexcept -> std::string;

// A #line filename is Unicode text, not a sequence of execution-encoding bytes.
auto cpp_filename_token(std::string_view text) noexcept -> std::string;
