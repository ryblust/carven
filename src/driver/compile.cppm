module carven:driver.compile;

import :driver.options;
import std;

auto run_compilation_command(std::span<const std::string> args, CompilationCommand command) noexcept
    -> int;
