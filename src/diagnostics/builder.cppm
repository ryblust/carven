module carven:diagnostics.builder;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.text;
import std;

class DiagnosticBuilder final {
public:
    DiagnosticBuilder(DiagnosticCode code, std::string message) noexcept;

    auto primary(SourceSpan span, std::string message = {}) noexcept -> DiagnosticBuilder&;
    auto related(SourceSpan span, std::string message = {}) noexcept -> DiagnosticBuilder&;
    auto note(std::string message, std::optional<SourceSpan> span = std::nullopt) noexcept
        -> DiagnosticBuilder&;
    auto build() noexcept -> Diagnostic;

private:
    Diagnostic diagnostic;
};
