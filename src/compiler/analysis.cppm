module carven:compiler.analysis;

import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.analysis.source;
import :semantic.evaluation.execution;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :support.timing;
import std;

struct CompilationOutput final {
    ExecutionOutputStream stream;
    std::string bytes;
};

struct CompilationAnalysisOptions final {
    ExecutionOutput output;
    TimingOutput timings;
    bool collect_source_observations;
    bool capture_output;
    HostEffects host_effects;
    std::optional<SourceSpan> completion;
    // Source queries collect detached observations but never publish a program.
    bool source_query;
    std::span<const SourceSpan> excluded;
    AnalysisNativeCall native_call;
};

// Owns a complete analysis transaction, including failed-source diagnostics.
// Source observations and published identities borrow only this result's owners.
class CompilationAnalysis final {
public:
    CompilationAnalysis(const CompilationAnalysis&) = delete;
    CompilationAnalysis(CompilationAnalysis&&) = default;
    ~CompilationAnalysis() = default;
    auto operator=(const CompilationAnalysis&) -> CompilationAnalysis& = delete;
    auto operator=(CompilationAnalysis&&) -> CompilationAnalysis& = delete;

    static auto failed(SourceManager sources, Diagnostics diagnostics) noexcept
        -> CompilationAnalysis;

    auto program() const noexcept -> const SemIRProgram*;
    auto sources() const noexcept -> const SourceManager&;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;
    auto output() const noexcept -> std::span<const CompilationOutput>;
    auto source_occurrences() const noexcept -> std::span<const SourceOccurrence>;
    // Requested source facts cover completed construction and solving. Validation
    // may still have failed; only program()/take_checked_unit() expose publication.
    auto source_construction_complete() const noexcept -> bool;
    // Whether requested source observation ran, even if no facts survived.
    auto has_source_analysis() const noexcept -> bool;
    auto completion_context() const noexcept -> SourceCompletionContext;
    auto source_completions() const noexcept -> std::span<const SourceCompletionCandidate>;
    // The extracted published program independently retains its provenance.
    auto take_program() && noexcept -> std::optional<SemIRProgram>;
    auto take_checked_unit() && noexcept -> std::optional<CheckedUnit>;

private:
    friend auto analyze_checked_compilation(
        SourceManager sources,
        SourceBatch batch,
        std::optional<CheckedDefinitions> seed,
        bool retain_definitions,
        CompilationAnalysisOptions options,
        std::span<const ParsedSourceInput> parsed
    ) noexcept -> CompilationAnalysis;

    static auto analyze(
        SourceManager sources,
        SourceBatch batch,
        std::optional<CheckedDefinitions> seed,
        CompilationAnalysisOptions options,
        std::span<const ParsedSourceInput> parsed,
        bool retain_definitions
    ) noexcept -> CompilationAnalysis;

    explicit CompilationAnalysis(SourceManager sources) noexcept;

    SourceManager source_manager;
    std::optional<CheckedUnit> semantic;
    Diagnostics findings;
    std::vector<CompilationOutput> execution_output;
    std::vector<SourceOccurrence> occurrences;
    // Absent before semantic observation; false for partial construction.
    std::optional<bool> source_construction_completed;
    std::vector<SourceCompletionCandidate> completions;
    SourceCompletionContext completion_site = SourceCompletionContext::None;

    friend auto analyze_compilation(
        SourceManager sources,
        SourceBatch batch,
        CompilationAnalysisOptions options,
        std::span<const ParsedSourceInput> parsed
    ) noexcept -> CompilationAnalysis;
};

auto analyze_compilation(
    SourceManager sources,
    SourceBatch batch,
    CompilationAnalysisOptions options,
    std::span<const ParsedSourceInput> parsed = {}
) noexcept -> CompilationAnalysis;

auto analyze_checked_compilation(
    SourceManager sources,
    SourceBatch batch,
    std::optional<CheckedDefinitions> seed,
    bool retain_definitions,
    CompilationAnalysisOptions options,
    std::span<const ParsedSourceInput> parsed = {}
) noexcept -> CompilationAnalysis;
