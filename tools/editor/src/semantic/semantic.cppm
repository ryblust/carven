module carven:editor.semantic;

import :diagnostics.diagnostic;
import :editor.document;
import :semantic.analysis.source;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.text;
import std;

namespace editor {

struct ProjectModule final {
    std::string document;
    CanonicalModulePath module_path;

    auto operator==(const ProjectModule&) const noexcept -> bool = default;
};

struct SemanticInput final {
    ProjectModule module;
    // A missing document remains an explicit input error.
    std::shared_ptr<const DocumentSource> source;
};

struct SemanticOutput final {
    ExecutionOutputStream stream;
    std::string bytes;
};

struct DocumentLocation final {
    std::string document;
    Span range;
};

struct HoverInformation final {
    DocumentLocation location;
    SourceType type;
};

// One complete compilation result, including failed analysis and captured static
// output. Source and semantic borrows remain valid while this owner is alive.
// Client versions belong to the enclosing snapshot, not this content result.
class SemanticAnalysis final {
public:
    auto program() const noexcept -> const SemIRProgram*;
    auto sources() const noexcept -> const SourceManager&;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;
    auto output() const noexcept -> std::span<const SemanticOutput>;
    // Published TypeIDs belong to program(); failed analysis retains only
    // concrete builtin construction types from completed source bodies.
    auto hover(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<HoverInformation>;
    auto definition(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<DocumentLocation>;
    // Includes declaration occurrences. Unknown targets return no result.
    auto references(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<std::vector<DocumentLocation>>;
    auto source(std::string_view document) const noexcept -> std::optional<SourceView>;

private:
    SemanticAnalysis(
        SourceManager sources,
        std::map<std::string, SourceID, std::less<>> document_sources,
        std::optional<SemIRProgram> program,
        Diagnostics diagnostics,
        std::vector<SemanticOutput> output,
        std::vector<SourceOccurrence> occurrences
    ) noexcept;

    auto select(std::string_view document, std::uint32_t offset) const noexcept
        -> const SourceOccurrence*;
    auto locate_source(SourceSpan location) const noexcept -> std::optional<DocumentLocation>;

    SourceManager source_manager;
    std::map<std::string, SourceID, std::less<>> source_ids;
    std::optional<SemIRProgram> semantic_program;
    Diagnostics findings;
    std::vector<SemanticOutput> execution_output;
    std::map<SourceID, std::vector<SourceOccurrence>> source_occurrences;

    friend auto analyze_project(std::span<const SemanticInput> inputs) noexcept
        -> std::shared_ptr<const SemanticAnalysis>;
};

// Uses the compiler's full analysis as a coarse query provider. Explicit module
// paths define imports; document identities are never interpreted as file paths.
auto analyze_project(std::span<const SemanticInput> inputs) noexcept
    -> std::shared_ptr<const SemanticAnalysis>;

} // namespace editor
