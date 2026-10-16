module carven:driver.analysis;

import :driver.sources;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

// Loads the collected source batch with its resolved module identities.
auto load_and_analyze_sources(
    std::span<const SourceInput> inputs,
    ExecutionOutput output,
    TimingOutput timings = {}
) noexcept -> std::optional<SemIRProgram>;

auto load_and_analyze_checked_sources(
    std::span<const SourceInput> inputs,
    std::optional<CheckedDefinitions> seed,
    bool retain_definitions,
    ExecutionOutput output,
    TimingOutput timings = {}
) noexcept -> std::optional<CheckedUnit>;
