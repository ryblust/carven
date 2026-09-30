module carven:semantic.evaluation.display;

import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.constant_access;
import std;

auto display_execution_value(
    const ExecutionValueAccess& values,
    const ExecutionValue& value,
    bool nested = false,
    const ExecutionMemory* memory = nullptr
) noexcept -> std::optional<std::string>;

// Appends one field of an execution report in the layout of the native
// runtime: a one-line value follows its label and a longer one forms a block.
auto append_report_field(
    std::string& output,
    std::string_view label,
    std::string_view text,
    std::string_view indent = "  "
) noexcept -> void;

auto execution_message(const ExecutionEvent& event) noexcept -> std::string;
// Counts exactly the same report fragments without allocating a joined message.
auto execution_message_size(const ExecutionEvent& event) noexcept -> std::size_t;
