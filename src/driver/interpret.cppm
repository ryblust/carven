module carven:driver.interpret;

import :semantic.evaluation.native;
import std;

auto run_interpret_command(
    std::span<const std::string> args,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> int;

auto run_query_command(
    std::span<const std::string> args,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> int;

// Invoke a parameterless library function through ordinary analysis and execution.
auto run_interpret_function(
    std::string_view module_path,
    std::string_view function_name,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> int;
