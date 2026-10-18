module carven:driver.interpret;

import std;

auto run_interpret_command(std::string_view executable, std::span<const std::string> args) noexcept
    -> int;
