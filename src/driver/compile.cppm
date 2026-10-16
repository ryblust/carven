module carven:driver.compile;

import :driver.options;
import std;

auto run_compilation_command(
    std::string_view executable,
    std::span<const char* const> args,
    CompilationCommand command
) noexcept -> int;
