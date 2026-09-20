module carven:diagnostics.report;

import :diagnostics.diagnostic;
import :source.manager;
import :source.text;
import std;

auto render_diagnostic(
    const Diagnostic& diagnostic,
    const SourceManager& sources,
    bool use_color = false
) noexcept -> std::string;

auto render_diagnostics(
    std::span<const Diagnostic> diagnostics,
    const SourceManager& sources,
    bool use_color = false
) noexcept -> std::string;
