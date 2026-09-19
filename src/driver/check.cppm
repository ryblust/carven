module carven:driver.check;

import std;

auto run_check_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int;
