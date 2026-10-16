module carven:compiler.analysis.impl;

import :compiler.analysis;
import :frontend.program.parse;
import :semantic.analysis.source;
import :semantic.analyze;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.provenance;
import std;

CompilationAnalysis::CompilationAnalysis(SourceManager sources) noexcept
    : source_manager(std::move(sources)) {}

auto CompilationAnalysis::failed(SourceManager sources, Diagnostics diagnostics) noexcept
    -> CompilationAnalysis {
    auto result = CompilationAnalysis(std::move(sources));
    result.findings = std::move(diagnostics);
    return result;
}

auto CompilationAnalysis::program() const noexcept -> const SemIRProgram* {
    return semantic ? std::addressof(semantic->program) : nullptr;
}

auto CompilationAnalysis::sources() const noexcept -> const SourceManager& {
    return source_manager;
}

auto CompilationAnalysis::diagnostics() const noexcept -> std::span<const Diagnostic> {
    return findings;
}

auto CompilationAnalysis::output() const noexcept -> std::span<const CompilationOutput> {
    return execution_output;
}

auto CompilationAnalysis::source_occurrences() const noexcept -> std::span<const SourceOccurrence> {
    return occurrences;
}

auto CompilationAnalysis::take_program() && noexcept -> std::optional<SemIRProgram> {
    return semantic ? std::optional(std::move(semantic->program)) : std::nullopt;
}

auto CompilationAnalysis::take_checked_unit() && noexcept -> std::optional<CheckedUnit> {
    return std::move(semantic);
}

auto CompilationAnalysis::analyze(
    SourceManager sources,
    SourceBatch batch,
    std::optional<CheckedDefinitions> seed,
    CompilationAnalysisOptions options,
    std::span<const ParsedSourceInput> parsed,
    bool retain_definitions
) noexcept -> CompilationAnalysis {
    auto result = CompilationAnalysis(std::move(sources));
    auto imported = seed ? std::optional(seed->take_provenance(result.source_manager))
                         : std::optional<CompilationProvenance>();
    auto syntax =
        parse_program(result.sources(), batch, options.timings, parsed, std::move(imported));
    if (!syntax) {
        result.findings = std::move(syntax.error());
        return result;
    }
    const auto capture = [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
        if (options.capture_output) {
            result.execution_output.push_back({.stream = stream, .bytes = std::string(bytes)});
        }
        if (options.output) {
            options.output(stream, bytes);
        }
    };
    const auto collect = [&](std::span<const SourceOccurrence> facts) noexcept {
        result.occurrences.assign(facts.begin(), facts.end());
    };
    auto analyzed = analyze_checked(
        std::move(*syntax),
        std::move(seed),
        options.capture_output || options.output ? ExecutionOutput(capture) : ExecutionOutput {},
        options.timings,
        options.collect_source_observations ? SourceAnalysisOutput(collect)
                                            : SourceAnalysisOutput {},
        options.host_effects,
        retain_definitions
    );
    if (analyzed) {
        result.semantic.emplace(std::move(analyzed->value));
        result.findings = std::move(analyzed->diagnostics);
    } else {
        result.findings = std::move(analyzed.error());
    }
    return result;
}

auto analyze_compilation(
    SourceManager sources,
    SourceBatch batch,
    CompilationAnalysisOptions options,
    std::span<const ParsedSourceInput> parsed
) noexcept -> CompilationAnalysis {
    return CompilationAnalysis::analyze(
        std::move(sources),
        batch,
        std::nullopt,
        options,
        parsed,
        false
    );
}

auto analyze_checked_compilation(
    SourceManager sources,
    SourceBatch batch,
    std::optional<CheckedDefinitions> seed,
    bool retain_definitions,
    CompilationAnalysisOptions options,
    std::span<const ParsedSourceInput> parsed
) noexcept -> CompilationAnalysis {
    return CompilationAnalysis::analyze(
        std::move(sources),
        batch,
        std::move(seed),
        options,
        parsed,
        retain_definitions
    );
}
