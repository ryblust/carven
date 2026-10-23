module carven:driver.compile;

import :driver.options;
import :semantic.evaluation.native;
import std;

auto run_compilation_command(
    std::span<const std::string> args,
    CompilationCommand command,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> int;
