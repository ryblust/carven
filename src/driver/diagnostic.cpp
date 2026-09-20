module carven:driver.diagnostic.impl;

import :diagnostics.report;
import :driver.diagnostic;
import :support.terminal;
import std;

auto render_driver_error(
    std::string_view message,
    bool use_color,
    std::string_view usage_command,
    std::string_view help
) noexcept -> std::string {
    const auto styler = TerminalStyler(use_color);
    auto result = std::format("carven: {} {}\n", styler.bold_red("error:"), message);
    if (!help.empty()) {
        result += std::format("  {} {}\n", styler.bold_green("help:"), help);
    }
    if (!usage_command.empty()) {
        result += std::format("Run '{} --help' for usage.\n", usage_command);
    }
    return result;
}

auto emit_driver_error(
    std::string_view message,
    std::string_view usage_command,
    std::string_view help
) noexcept -> int {
    std::print(
        std::cerr,
        "{}",
        render_driver_error(message, initialize_diagnostic_color(), usage_command, help)
    );
    return 1;
}

auto emit_missing_entry_error() noexcept -> int {
    return emit_driver_error(
        "running a program requires a runtime entry point",
        {},
        "define 'fn main()' or top-level runtime statements. "
        "Constant blocks execute during semantic analysis and do not define a runtime entry. "
        "Use 'carven check <source-file>' for analysis and constant evaluation only"
    );
}

auto initialize_diagnostic_color() noexcept -> bool {
    if (const auto* no_color = std::getenv("NO_COLOR"); no_color != nullptr && *no_color != '\0') {
        return false;
    }
    if (const auto* term = std::getenv("TERM");
        term != nullptr && std::string_view(term) == "dumb") {
        return false;
    }
    return enable_stderr_ansi();
}

auto emit_source_diagnostics(
    std::span<const Diagnostic> diagnostics,
    const SourceManager& sources
) noexcept -> void {
    std::print(
        std::cerr,
        "{}",
        render_diagnostics(diagnostics, sources, initialize_diagnostic_color())
    );
}
