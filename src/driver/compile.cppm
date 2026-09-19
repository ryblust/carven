module carven:driver.compile;

import std;

auto run_compile_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int;
