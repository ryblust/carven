module carven:driver.analysis.impl;

import :compiler.analysis;
import :driver.analysis;
import :driver.diagnostic;
import :driver.sources;
import :source.batch;
import :source.manager;
import :source.text;
import :support.timing;
import std;

auto load_and_analyze_sources(
    std::span<const SourceInput> inputs,
    const ExecutionOutput& output,
    TimingRecorder* timings
) noexcept -> std::optional<SemIRProgram> {
    auto loading = TimingScope(timings, TimingStage::SourceLoading);
    auto has_error = false;
    auto sources = SourceManager();
    auto module_inputs = std::vector<SourceModuleInput> {};
    module_inputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        const auto source_id = sources.append_file(input.path);
        if (!source_id) {
            emit_driver_error(
                std::format("{}: '{}'", source_id.error().message, source_id.error().origin)
            );
            has_error = true;
            continue;
        }
        module_inputs.push_back({
            .source_id = *source_id,
            .module_path = input.module_path,
        });
    }
    loading.stop();
    if (has_error) {
        return std::nullopt;
    }

    auto result =
        analyze_compilation(sources, SourceBatch {.modules = module_inputs}, output, timings);
    if (!result) {
        emit_source_diagnostics(result.error(), sources);
        return std::nullopt;
    }
    if (!result->diagnostics.empty()) {
        emit_source_diagnostics(result->diagnostics, sources);
    }
    return std::move(result->value);
}
