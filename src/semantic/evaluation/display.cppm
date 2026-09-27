module carven:semantic.evaluation.display;

import :semantic.evaluation.value;
import :semantic.semir.constant_access;
import std;

auto display_execution_value(
    const ExecutionValueAccess& values,
    const ExecutionValue& value,
    bool nested = false,
    const ExecutionMemory* memory = nullptr
) noexcept -> std::optional<std::string>;
