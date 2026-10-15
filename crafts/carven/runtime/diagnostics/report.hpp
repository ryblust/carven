#pragma once

#include <carven/runtime/diagnostics/trap.hpp>

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace carven::runtime {

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
