module carven:driver.analysis.impl;

import :compiler.analysis;
import :driver.analysis;
import :driver.diagnostic;
import :driver.sources;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.evaluation.execution;
import :source.batch;
import :source.manager;
import :source.text;
import :support.timing;
import std;

namespace {

auto analyze_sources(
    std::span<const SourceInput> inputs,
    std::optional<CheckedDefinitions> seed,
    ExecutionOutput output,
    TimingOutput timings,
    bool retain_definitions
) noexcept -> std::optional<CompilationAnalysis> {
    auto loading = TimingScope(timings, TimingStage::SourceLoading);
    auto has_error = false;
    auto sources = SourceManager();
    auto modules = std::vector<SourceModuleInput>();
    modules.reserve(inputs.size());
    for (const auto& input : inputs) {
        const auto source_id = sources.append_file(input.path);
        if (!source_id) {
            emit_driver_error(
                std::format("{}: '{}'", source_id.error().message, source_id.error().origin)
            );
            has_error = true;
            continue;
        }
        modules.push_back({.source_id = *source_id, .module_path = input.module_path});
    }
    loading.stop();
    if (has_error) {
        return std::nullopt;
    }
    const auto options = CompilationAnalysisOptions {
        .output = output,
        .timings = timings,
        .collect_source_observations = false,
        .capture_output = false,
        .host_effects = HostEffects::Allow,
    };
    const auto batch = SourceBatch {.modules = modules};
    auto result = analyze_checked_compilation(
        std::move(sources),
        batch,
        std::move(seed),
        retain_definitions,
        options
    );
    if (!result.diagnostics().empty()) {
        emit_source_diagnostics(result.diagnostics(), result.sources());
    }
    return result;
}

} // namespace

auto load_and_analyze_sources(
    std::span<const SourceInput> inputs,
    ExecutionOutput output,
    TimingOutput timings
) noexcept -> std::optional<SemIRProgram> {
    auto result = analyze_sources(inputs, std::nullopt, output, timings, false);
    return result ? std::move(*result).take_program() : std::nullopt;
}

auto load_and_analyze_checked_sources(
    std::span<const SourceInput> inputs,
    std::optional<CheckedDefinitions> seed,
    bool retain_definitions,
    ExecutionOutput output,
    TimingOutput timings
) noexcept -> std::optional<CheckedUnit> {
    auto result = analyze_sources(inputs, std::move(seed), output, timings, retain_definitions);
    return result ? std::move(*result).take_checked_unit() : std::nullopt;
}
