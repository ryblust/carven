module carven:driver.run;

import std;

auto run_native_command(std::string_view executable, std::span<const char* const> args) noexcept
    -> int;
