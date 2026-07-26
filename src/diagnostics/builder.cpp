module carven:diagnostics.builder.impl;

import :diagnostics.builder;
import :diagnostics.code;
import std;

DiagnosticBuilder::DiagnosticBuilder(DiagnosticCode code, std::string message) noexcept
    : diagnostic {
          .finding =
              {
                  .severity = diagnostic_code_info(code).default_severity,
                  .code = code,
                  .message = std::move(message),
              },
          .attachment = {
              .primary = std::nullopt,
              .related = {},
              .notes = {},
          },
      } {}

auto DiagnosticBuilder::primary(SourceSpan span, std::string message) noexcept
    -> DiagnosticBuilder& {
    diagnostic.attachment.primary = {
        .span = span,
        .message = std::move(message),
    };
    return *this;
}

auto DiagnosticBuilder::related(SourceSpan span, std::string message) noexcept
    -> DiagnosticBuilder& {
    diagnostic.attachment.related.push_back({
        .span = span,
        .message = std::move(message),
    });
    return *this;
}

auto DiagnosticBuilder::note(std::string message, std::optional<SourceSpan> span) noexcept
    -> DiagnosticBuilder& {
    diagnostic.attachment.notes.push_back({
        .message = std::move(message),
        .span = span,
    });
    return *this;
}

auto DiagnosticBuilder::build() noexcept -> Diagnostic {
    return std::move(diagnostic);
}
