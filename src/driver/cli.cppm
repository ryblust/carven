module carven:driver.cli;

import :semantic.evaluation.native;
import std;

// Normalizes process arguments and dispatches commands with the supplied execution providers.
auto carven_main(
    int argc,
    const char* const* argv,
    std::span<const ExecutionNativeBinding> native_bindings
) noexcept -> int;
