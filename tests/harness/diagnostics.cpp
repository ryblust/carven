module carven:test.harness.diagnostics.impl;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :test.harness.diagnostics;
import :test.harness.framework;
import std;

namespace {

auto describe_diagnostics(std::ostream& output, std::span<const Diagnostic> diagnostics) noexcept
    -> void {
    std::print(output, "\n  actual diagnostics:");
    if (diagnostics.empty()) {
        std::print(output, " (none)");
        return;
    }
    for (const auto& diagnostic : diagnostics) {
        std::print(
            output,
            "\n    {}: {}",
            diagnostic_code_info(diagnostic.finding.code).name,
            diagnostic.finding.message
        );
    }
}

} // namespace

auto find_diagnostic(std::span<const Diagnostic> diagnostics, DiagnosticCode code) noexcept
    -> const Diagnostic* {
    const auto found =
        std::ranges::find_if(diagnostics, [code](const Diagnostic& diagnostic) noexcept {
            return diagnostic.finding.code == code;
        });
    return found == diagnostics.end() ? nullptr : std::addressof(*found);
}

auto expect_diagnostic(
    std::span<const Diagnostic> diagnostics,
    DiagnosticCode code,
    std::source_location location
) noexcept -> TestAssertion {
    return TestAssertion(
        find_diagnostic(diagnostics, code) != nullptr,
        false,
        location,
        [&](std::ostream& output) noexcept {
            std::print(output, "\n  expected diagnostic: {}", diagnostic_code_info(code).name);
            describe_diagnostics(output, diagnostics);
        }
    );
}

auto expect_no_diagnostic(
    std::span<const Diagnostic> diagnostics,
    DiagnosticCode code,
    std::source_location location
) noexcept -> TestAssertion {
    return TestAssertion(
        find_diagnostic(diagnostics, code) == nullptr,
        false,
        location,
        [&](std::ostream& output) noexcept {
            std::print(output, "\n  unexpected diagnostic: {}", diagnostic_code_info(code).name);
            describe_diagnostics(output, diagnostics);
        }
    );
}
