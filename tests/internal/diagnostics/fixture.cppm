module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.fixture;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :diagnostics.report;
import :source.manager;
import :source.text;
import std;

auto localized(Diagnostic diagnostic, SourceID source_id) noexcept -> Diagnostic {
    if (diagnostic.attachment.primary.has_value()) {
        diagnostic.attachment.primary->span.source_id = source_id;
    }
    for (auto& label : diagnostic.attachment.related) {
        label.span.source_id = source_id;
    }
    return diagnostic;
}

auto make_diagnostic(
    std::string message,
    DiagnosticLabel primary,
    std::vector<DiagnosticLabel> related = {}
) noexcept -> Diagnostic {
    return {
        .finding =
            {
                .severity = DiagnosticSeverity::Error,
                .code = DiagnosticCode::Lexical,
                .message = std::move(message),
            },
        .attachment = {
            .primary = std::move(primary),
            .related = std::move(related),
            .notes = {},
        },
    };
}

auto render_diagnostic(const Diagnostic& diagnostic, SourceView source) noexcept -> std::string {
    auto sources = SourceManager();
    const auto source_id =
        *sources.append_virtual(std::string(source.origin), std::string(source.text));
    return ::render_diagnostic(localized(diagnostic, source_id), sources);
}

auto render_diagnostics(std::span<const Diagnostic> diagnostics, SourceView source) noexcept
    -> std::string {
    auto sources = SourceManager();
    const auto source_id =
        *sources.append_virtual(std::string(source.origin), std::string(source.text));
    auto values = Diagnostics();
    values.reserve(diagnostics.size());
    for (const auto& diagnostic : diagnostics) {
        values.push_back(localized(diagnostic, source_id));
    }
    return ::render_diagnostics(values, sources);
}
