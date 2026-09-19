module carven:driver.interpret;

import std;

auto run_interpret_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int;
