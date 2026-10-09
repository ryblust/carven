module carven:compiler.analysis.impl;

import :compiler.analysis;
import :frontend.program.parse;
import :semantic.analysis.source;
import :semantic.analyze;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
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
    return semantic_program ? std::addressof(*semantic_program) : nullptr;
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
    return std::move(semantic_program);
}

auto analyze_compilation(
    SourceManager sources,
    SourceBatch batch,
    CompilationAnalysisOptions options,
    std::span<const ParsedSourceInput> parsed
) noexcept -> CompilationAnalysis {
    auto result = CompilationAnalysis(std::move(sources));
    auto syntax = parse_program(result.sources(), batch, options.timings, parsed);
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
    auto analyzed = analyze(
        std::move(*syntax),
        options.capture_output || options.output ? ExecutionOutput(capture) : ExecutionOutput {},
        options.timings,
        options.collect_source_observations ? SourceAnalysisOutput(collect)
                                            : SourceAnalysisOutput {}
    );
    if (analyzed) {
        result.semantic_program.emplace(std::move(analyzed->value));
        result.findings = std::move(analyzed->diagnostics);
    } else {
        result.findings = std::move(analyzed.error());
    }
    return result;
}
