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

auto CompilationAnalysis::source_construction_complete() const noexcept -> bool {
    return source_construction_completed.value_or(false);
}

auto CompilationAnalysis::has_source_analysis() const noexcept -> bool {
    return source_construction_completed.has_value();
}

auto CompilationAnalysis::take_program() && noexcept -> std::optional<SemIRProgram> {
    return semantic ? std::optional(std::move(semantic->program)) : std::nullopt;
}

auto CompilationAnalysis::take_checked_unit() && noexcept -> std::optional<CheckedUnit> {
    return std::move(semantic);
}

auto CompilationAnalysis::completion_context() const noexcept -> SourceCompletionContext {
    return completion_site;
}

auto CompilationAnalysis::source_completions() const noexcept
    -> std::span<const SourceCompletionCandidate> {
    return completions;
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
    auto syntax = parse_program(
        result.sources(),
        batch,
        options.timings,
        parsed,
        std::move(imported),
        options.source_query
    );
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
    const auto collect = [&](SourceAnalysisView facts) noexcept {
        result.occurrences.assign(facts.occurrences.begin(), facts.occurrences.end());
        result.source_construction_completed = facts.construction_complete;
    };
    const auto complete = [&](std::span<const SourceCompletionCandidate> candidates,
                              SourceCompletionContext context) noexcept {
        result.completion_site = context;
        result.completions.assign(candidates.begin(), candidates.end());
    };
    auto analyzed = analyze_checked(
        std::move(*syntax),
        options.host_effects,
        retain_definitions,
        options.source_query,
        std::move(seed),
        options.capture_output || options.output ? ExecutionOutput(capture) : ExecutionOutput {},
        options.timings,
        options.collect_source_observations ? SourceAnalysisOutput(collect)
                                            : SourceAnalysisOutput {},
        {.position = options.completion,
         .output =
             options.completion ? SourceCompletionOutput(complete) : SourceCompletionOutput {},
         .excluded = options.excluded},
        options.native_call
    );
    if (analyzed) {
        if (!options.source_query) {
            result.semantic.emplace(std::move(analyzed->value));
        }
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
