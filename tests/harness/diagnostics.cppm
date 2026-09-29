module carven:test.harness.diagnostics;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :test.harness.framework;
import std;

namespace carven::testing {

auto find_diagnostic(std::span<const Diagnostic> diagnostics, DiagnosticCode code) noexcept
    -> const Diagnostic*;

auto expect_diagnostic(
    std::span<const Diagnostic> diagnostics,
    DiagnosticCode code,
    std::source_location location = std::source_location::current()
) noexcept -> Assertion;

auto expect_no_diagnostic(
    std::span<const Diagnostic> diagnostics,
    DiagnosticCode code,
    std::source_location location = std::source_location::current()
) noexcept -> Assertion;

} // namespace carven::testing
