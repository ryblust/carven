module carven:compiler.analysis;

import :diagnostics.diagnostic;
import :frontend.program.parse;
import :semantic.analysis.source;
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
    // The extracted published program independently retains its provenance.
    auto take_program() && noexcept -> std::optional<SemIRProgram>;

private:
    explicit CompilationAnalysis(SourceManager sources) noexcept;

    SourceManager source_manager;
    std::optional<SemIRProgram> semantic_program;
    Diagnostics findings;
    std::vector<CompilationOutput> execution_output;
    std::vector<SourceOccurrence> occurrences;

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
    CompilationAnalysisOptions options =
        {.output = {},
         .timings = {},
         .collect_source_observations = false,
         .capture_output = false},
    std::span<const ParsedSourceInput> parsed = {}
) noexcept -> CompilationAnalysis;
