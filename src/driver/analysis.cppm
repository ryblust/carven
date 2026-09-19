module carven:driver.analysis;

import :driver.sources;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

// Loads the collected source batch with its resolved module identities.
auto load_and_analyze_sources(
    std::span<const SourceInput> inputs,
    const ExecutionOutput& output,
    TimingRecorder* timings = nullptr
) noexcept -> std::optional<SemIRProgram>;
