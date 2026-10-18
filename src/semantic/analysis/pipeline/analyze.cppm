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
    HostEffects host_effects,
    ExecutionOutput output = {},
    TimingOutput timings = {},
    SourceAnalysisOutput source_output = {}
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics>;

// A seed contributes the checked prefix whose provenance parse_program has
// already consumed while appending this compilation's source modules.
auto analyze_checked(
    SyntaxProgram syntax,
    HostEffects host_effects,
    bool retain_definitions,
    std::optional<CheckedDefinitions> seed = std::nullopt,
    ExecutionOutput output = {},
    TimingOutput timings = {},
    SourceAnalysisOutput source_output = {}
) noexcept -> std::expected<Diagnosed<CheckedUnit>, Diagnostics>;
