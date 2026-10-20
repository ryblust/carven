module carven:driver.analysis;

import :driver.sources;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

// Loads explicit roots and their available source dependencies. The inventory
// retains only loaded modules; parsed documents feed the same analysis once.
auto load_and_analyze_sources(
    CommandSources& inputs,
    ExecutionOutput output,
    TimingOutput timings = {}
) noexcept -> std::optional<SemIRProgram>;

auto load_and_analyze_checked_sources(
    CommandSources& inputs,
    std::optional<CheckedDefinitions> seed,
    bool retain_definitions,
    ExecutionOutput output,
    TimingOutput timings = {}
) noexcept -> std::optional<CheckedUnit>;
