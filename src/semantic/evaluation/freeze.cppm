module carven:semantic.evaluation.freeze;

import :semantic.evaluation.value;
import std;

auto freeze_constant_value(ConstantValueAccess& values, ConstantExecutionValue value) noexcept
    -> std::optional<ConstantID>;

// Retain a completed array's contents in a read-only slice with the same element type.
