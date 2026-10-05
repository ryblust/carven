module carven:workspace.semantic;

import :diagnostics.diagnostic;
import :semantic.analysis.source;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.text;
import :workspace.document;
import std;

struct WorkspaceProjectModule final {
    std::string document;
    CanonicalModulePath module_path;

    auto operator==(const WorkspaceProjectModule&) const noexcept -> bool = default;
};

struct WorkspaceSemanticInput final {
    WorkspaceProjectModule module;
    // A missing document remains an explicit input error.
    std::shared_ptr<const WorkspaceDocumentSource> source;
};

struct WorkspaceSemanticOutput final {
    ExecutionOutputStream stream;
    std::string bytes;
};

struct WorkspaceDocumentLocation final {
    std::string document;
    Span range;
};

struct WorkspaceHoverInformation final {
    WorkspaceDocumentLocation location;
    SourceType type;
};

// One complete compilation result, including failed analysis and captured static
// output. Source and semantic borrows remain valid while this owner is alive.
// Client versions belong to the enclosing snapshot, not this content result.
class WorkspaceSemanticAnalysis final {
public:
    auto program() const noexcept -> const SemIRProgram*;
    auto sources() const noexcept -> const SourceManager&;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;
    auto output() const noexcept -> std::span<const WorkspaceSemanticOutput>;
    // Published TypeIDs belong to program(); failed analysis retains only
    // concrete builtin construction types from completed source bodies.
    auto hover(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<WorkspaceHoverInformation>;
    auto definition(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<WorkspaceDocumentLocation>;
    // Includes declaration occurrences. Unknown targets return no result.
    auto references(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<std::vector<WorkspaceDocumentLocation>>;
    auto source(std::string_view document) const noexcept -> std::optional<SourceView>;

private:
    WorkspaceSemanticAnalysis(
        SourceManager sources,
        std::map<std::string, SourceID, std::less<>> document_sources,
        std::optional<SemIRProgram> program,
        Diagnostics diagnostics,
        std::vector<WorkspaceSemanticOutput> output,
        std::vector<SourceOccurrence> occurrences
    ) noexcept;

    auto select(std::string_view document, std::uint32_t offset) const noexcept
        -> const SourceOccurrence*;
    auto locate_source(SourceSpan location) const noexcept
        -> std::optional<WorkspaceDocumentLocation>;

    SourceManager source_manager;
    std::map<std::string, SourceID, std::less<>> source_ids;
    std::optional<SemIRProgram> semantic_program;
    Diagnostics findings;
    std::vector<WorkspaceSemanticOutput> execution_output;
    std::map<SourceID, std::vector<SourceOccurrence>> source_occurrences;

    friend auto analyze_workspace_project(std::span<const WorkspaceSemanticInput> inputs) noexcept
        -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
};

// Uses the compiler's full analysis as a coarse query provider. Explicit module
// paths define imports; document identities are never interpreted as file paths.
auto analyze_workspace_project(std::span<const WorkspaceSemanticInput> inputs) noexcept
    -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
