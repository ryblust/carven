module carven:driver.interpret;

import std;

auto run_interpret_command(std::span<const std::string> args) noexcept -> int;

// Invoke a parameterless library function through ordinary analysis and execution.
auto run_interpret_function(std::string_view module_path, std::string_view function_name) noexcept
    -> int;
