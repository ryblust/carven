module carven:semantic.analyze;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.program;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.analysis.source;
import :semantic.evaluation.execution;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

auto analyze(
    SyntaxProgram syntax,
    ExecutionOutput output = {},
    TimingOutput timings = {},
    SourceAnalysisOutput source_output = {},
    HostEffects host_effects = HostEffects::Deny
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics>;

// A seed contributes the checked prefix whose provenance parse_program has
// already consumed while appending this compilation's source modules.
auto analyze_checked(
    SyntaxProgram syntax,
    std::optional<CheckedDefinitions> seed = std::nullopt,
    ExecutionOutput output = {},
    TimingOutput timings = {},
    SourceAnalysisOutput source_output = {},
    HostEffects host_effects = HostEffects::Deny,
    bool retain_definitions = true
) noexcept -> std::expected<Diagnosed<CheckedUnit>, Diagnostics>;
