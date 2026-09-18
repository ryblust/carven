module carven:driver.analysis;

import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

// Loads and analyzes the explicit source batch, reporting diagnostics to stderr.
auto load_and_analyze_sources(
    std::span<const std::string_view> input_paths,
    const ExecutionOutput& output,
    TimingRecorder* timings = nullptr
) noexcept -> std::optional<SemIRProgram>;
