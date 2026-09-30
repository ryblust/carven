#pragma once

#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <source_location>
#include <string_view>

namespace carven::runtime {

struct TestReportContext final {
    std::string_view module_name;
    std::string_view case_name;
};

inline thread_local TestReportContext* active_test_report = nullptr;

#if defined(_MSC_VER) && !defined(__clang__)
#define CARVEN_RUNTIME_COLD __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
#define CARVEN_RUNTIME_COLD [[gnu::cold, gnu::noinline]]
#else
#define CARVEN_RUNTIME_COLD
#endif

// Generated code names the Carven source position of an operation that can
// report. The file is a null-terminated literal; the record stays two machine
// words on 64-bit targets. Inlining the check lets the native optimizer defer
// materialization to the report path; the representation alone does not promise
// this. Native code that reaches a reporting operation names its own C++ position.
struct SourceSite final {
    constexpr SourceSite(
        const char* source_file,
        std::uint32_t source_line,
        std::uint32_t source_column
    ) noexcept
        : file(source_file),
          line(source_line),
          column(source_column) {}

    static constexpr auto native(
        std::source_location location = std::source_location::current()
    ) noexcept -> SourceSite {
        return SourceSite(location.file_name(), location.line(), location.column());
    }

    const char* file;
    std::uint32_t line;
    std::uint32_t column;
};

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

inline auto write_test_context() noexcept -> void {
    if (active_test_report != nullptr) {
        std::fputs("  test:\n", stderr);
        write_report_field("module:", active_test_report->module_name, "    ");
        write_report_field("name:", active_test_report->case_name, "    ");
    }
}

// Opens a report with its location, description, and active test.
CARVEN_RUNTIME_COLD inline auto begin_report(std::string_view reason, SourceSite site) noexcept
    -> void {
    std::fprintf(stderr, "%s:%" PRIu32 ":%" PRIu32 ": error: ", site.file, site.line, site.column);
    std::fwrite(reason.data(), 1, reason.size(), stderr);
    std::fputc('\n', stderr);
    write_test_context();
}

[[noreturn]] CARVEN_RUNTIME_COLD inline auto abort_report() noexcept -> void {
    std::fputs("  note: execution aborted\n\n", stderr);
    std::fflush(stderr);
    std::abort();
}

} // namespace detail

// Reports a violated runtime contract and terminates without stack cleanup.
[[noreturn]] CARVEN_RUNTIME_COLD inline auto trap(std::string_view reason, SourceSite site) noexcept
    -> void {
    detail::begin_report(reason, site);
    detail::abort_report();
}

} // namespace carven::runtime
