#pragma once

#include "display.hpp"

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace carven::runtime {

template<typename Left, typename EmitLeft, typename Right, typename EmitRight, typename Compare>
auto observe_comparison(
    DisplayWriter& writer,
    const StructuralDisplay<Left, EmitLeft>& left,
    const StructuralDisplay<Right, EmitRight>& right,
    Compare compare,
    std::string_view left_source,
    std::string_view right_source
) noexcept -> bool {
    const auto passed = compare(left.value, right.value);
    if (!passed) {
        writer.text(left_source);
        writer.text(": ");
        left.emit(writer, left.value);
        writer.text("\n");
        writer.text(right_source);
        writer.text(": ");
        right.emit(writer, right.value);
        writer.text("\n");
    }
    return passed;
}

inline auto observe_short_circuit(
    DisplayWriter& writer,
    bool left,
    std::optional<bool> right,
    std::string_view left_source,
    std::string_view right_source
) noexcept -> bool {
    const auto passed = right.value_or(left);
    if (!passed) {
        writer.text(left_source);
        writer.text(left ? ": true\n" : ": false\n");
        writer.text(right_source);
        writer.text(right ? ": false\n" : ": <not evaluated>\n");
    }
    return passed;
}

struct TestReportContext final {
    std::string_view module_name;
    std::string_view case_name;
};

inline thread_local TestReportContext* active_test_report = nullptr;

namespace detail {

inline auto write_report_field(
    std::string_view label,
    std::string_view text,
    std::string_view indent = "  "
) noexcept -> void {
    std::fwrite(indent.data(), 1, indent.size(), stderr);
    std::fwrite(label.data(), 1, label.size(), stderr);
    if (text.empty()) {
        std::fputs(" \"\"\n", stderr);
        return;
    }
    const auto block = text.find('\n') != std::string_view::npos;
    if (!block) {
        std::fputc(' ', stderr);
        std::fwrite(text.data(), 1, text.size(), stderr);
    } else {
        while (!text.empty()) {
            std::fputc('\n', stderr);
            std::fwrite(indent.data(), 1, indent.size(), stderr);
            std::fputs("  ", stderr);
            const auto newline = text.find('\n');
            const auto line = text.substr(0, newline);
            std::fwrite(line.data(), 1, line.size(), stderr);
            if (newline == std::string_view::npos) {
                break;
            }
            text.remove_prefix(newline + 1);
        }
    }
    std::fputc('\n', stderr);
}

} // namespace detail

inline auto write_failure(
    std::string_view file,
    std::uint32_t line,
    std::uint32_t column,
    std::string_view operation,
    std::optional<std::string_view> condition,
    std::optional<std::string_view> message,
    std::string_view explanation
) noexcept -> void {
    std::fwrite(file.data(), 1, file.size(), stderr);
    std::fprintf(stderr, ":%" PRIu32 ":%" PRIu32 ": error: ", line, column);
    if (operation == "assert") {
        std::fputs("assertion failed\n", stderr);
    } else if (operation == "require") {
        std::fputs("requirement failed\n", stderr);
    } else if (operation == "fail") {
        std::fputs("explicit failure\n", stderr);
    } else {
        std::fwrite(operation.data(), 1, operation.size(), stderr);
        std::fputs(" failed\n", stderr);
    }
    if (active_test_report != nullptr) {
        std::fputs("  test:\n", stderr);
        detail::write_report_field("module:", active_test_report->module_name, "    ");
        detail::write_report_field("name:", active_test_report->case_name, "    ");
    }
    if (condition) {
        detail::write_report_field("condition:", *condition);
    }
    if (!explanation.empty()) {
        detail::write_report_field("operands:", explanation);
    }
    if (message) {
        detail::write_report_field("message:", *message);
    }
    if (operation == "assert") {
        std::fputs("  note: execution aborted\n", stderr);
    } else if (operation == "require" || operation == "fail") {
        std::fputs("  note: test stopped\n", stderr);
    }
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

[[noreturn]] inline auto assertion_failed(
    std::string_view file,
    std::uint32_t line,
    std::uint32_t column,
    std::string_view operation,
    std::optional<std::string_view> condition,
    std::optional<std::string_view> message,
    std::string_view explanation
) noexcept -> void {
    write_failure(file, line, column, operation, condition, message, explanation);
    std::abort();
}

} // namespace carven::runtime
