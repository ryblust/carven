#pragma once

#include "display/display.hpp"
#include "trap.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <string_view>

namespace carven::runtime {

namespace detail {

inline auto observe_operand(
    DisplayWriter& writer,
    std::string_view source,
    std::string_view value
) noexcept -> void {
    if (source != value) {
        writer.text(source);
        writer.text(": ");
        writer.text(value);
        writer.text("\n");
    }
}

} // namespace detail

template<typename Left, typename EmitLeft, typename Right, typename EmitRight, typename Compare>
auto observe_comparison(
    DisplayWriter& writer,
    const StructuralDisplay<Left, EmitLeft>& left,
    const StructuralDisplay<Right, EmitRight>& right,
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): Comparators are borrowed and invoked as lvalues, including synchronous temporaries.
    Compare&& compare,
    std::string_view left_source,
    std::string_view right_source
) noexcept -> bool {
    const auto passed = std::invoke(compare, left.value, right.value);
    if (!passed) {
        // A literal operand displays as its own source text and explains nothing.
        const auto observe = [&](std::string_view source, const auto& operand) noexcept {
            auto value = DisplayWriter();
            std::invoke(operand.emit, value, operand.value, std::size_t {0});
            detail::observe_operand(writer, source, value.result());
        };
        observe(left_source, left);
        observe(right_source, right);
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
        // A literal operand displays as its own source text and explains nothing.
        detail::observe_operand(writer, left_source, left ? "true" : "false");
        detail::observe_operand(writer, right_source, right ? "false" : "<not evaluated>");
    }
    return passed;
}

inline auto write_failure(
    SourceSite site,
    std::string_view operation,
    std::optional<std::string_view> condition,
    std::optional<std::string_view> message,
    std::string_view explanation
) noexcept -> void {
    detail::begin_report(
        operation == "assert"        ? "assertion failed"
            : operation == "require" ? "requirement failed"
            : operation == "fail"    ? "explicit failure"
                                     : "check failed",
        site
    );
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
    SourceSite site,
    std::string_view operation,
    std::optional<std::string_view> condition,
    std::optional<std::string_view> message,
    std::string_view explanation
) noexcept -> void {
    write_failure(site, operation, condition, message, explanation);
    std::abort();
}

} // namespace carven::runtime
