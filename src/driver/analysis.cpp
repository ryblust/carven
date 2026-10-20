module carven:driver.analysis.impl;

import :compiler.analysis;
import :driver.analysis;
import :driver.diagnostic;
import :driver.sources;
import :frontend.document;
import :frontend.program.dependencies;
import :frontend.program.parse;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.evaluation.execution;
import :source.batch;
import :source.document;
import :source.manager;
import :source.text;
import :support.timing;
import std;

namespace {

auto analyze_sources(
    CommandSources& inputs,
    std::optional<CheckedDefinitions> seed,
    ExecutionOutput output,
    TimingOutput timings,
    bool retain_definitions
) noexcept -> std::optional<CompilationAnalysis> {
    auto has_error = false;
    auto sources = SourceManager();
    auto modules = std::vector<SourceModuleInput>();
    auto parsed = std::vector<ParsedSourceInput>();
    auto available = std::multimap<std::string_view, const SourceInput*, std::less<>>();
    for (const auto& input : inputs.carven) {
        available.emplace(input.module_path.value(), &input);
    }
    auto loaded = std::set<CanonicalModulePath>();
    const auto load =
        [&](const CanonicalModulePath& path) noexcept -> std::shared_ptr<const ParsedDocument> {
        auto first = std::shared_ptr<const ParsedDocument>();
        const auto [begin, end] = available.equal_range(path.value());
        for (auto iterator = begin; iterator != end; ++iterator) {
            const auto& input = *iterator->second;
            auto loading = TimingScope(timings, TimingStage::SourceLoading);
            const auto source_id = sources.append_file(input.path);
            loading.stop();
            if (!source_id) {
                emit_driver_error(
                    std::format("{}: '{}'", source_id.error().message, source_id.error().origin)
                );
                has_error = true;
                continue;
            }
            auto document = parse_document(SourceDocument::retain(sources, *source_id), timings);
            if (!first) {
                first = document;
            }
            modules.push_back({.source_id = *source_id, .module_path = input.module_path});
            parsed.push_back({.source_id = *source_id, .document = std::move(document)});
            loaded.insert(input.module_path);
        }
        return first;
    };
    static_cast<void>(source_dependency_closure(inputs.owned_modules, load));
    if (has_error) {
        return std::nullopt;
    }
    std::erase_if(inputs.carven, [&](const auto& input) noexcept {
        return !loaded.contains(input.module_path);
    });
    std::erase_if(inputs.standard_modules, [&](const auto& path) noexcept {
        return !loaded.contains(path);
    });
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
        options,
        parsed
    );
    if (!result.diagnostics().empty()) {
        emit_source_diagnostics(result.diagnostics(), result.sources());
    }
    return result;
}

} // namespace

auto load_and_analyze_sources(
    CommandSources& inputs,
    ExecutionOutput output,
    TimingOutput timings
) noexcept -> std::optional<SemIRProgram> {
    auto result = analyze_sources(inputs, std::nullopt, output, timings, false);
    return result ? std::move(*result).take_program() : std::nullopt;
}

auto load_and_analyze_checked_sources(
    CommandSources& inputs,
    std::optional<CheckedDefinitions> seed,
    bool retain_definitions,
    ExecutionOutput output,
    TimingOutput timings
) noexcept -> std::optional<CheckedUnit> {
    auto result = analyze_sources(inputs, std::move(seed), output, timings, retain_definitions);
    return result ? std::move(*result).take_checked_unit() : std::nullopt;
}
