module carven:diagnostics.report;

import :diagnostics.diagnostic;
import :source.manager;
import :source.text;
import std;

auto render_diagnostic(const Diagnostic& diagnostic, const SourceManager& sources) noexcept
    -> std::string;

auto render_diagnostics(
    std::span<const Diagnostic> diagnostics,
    const SourceManager& sources
) noexcept -> std::string;
