module carven:workspace.semantic;

import :compiler.analysis;
import :diagnostics.diagnostic;
import :frontend.document;
import :semantic.analysis.source;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.document;
import :source.manager;
import :source.module_path;
import :source.text;
import :support.timing;
import std;

struct WorkspaceProjectModule final {
    std::string document;
    CanonicalModulePath module_path;

    auto operator==(const WorkspaceProjectModule&) const noexcept -> bool = default;
};

struct WorkspaceSemanticInput final {
    WorkspaceProjectModule module;
    // A missing document remains an explicit input error.
    std::shared_ptr<const SourceDocument> source;
    std::shared_ptr<const ParsedDocument> parsed;
};

struct WorkspaceDocumentLocation final {
    std::string document;
    Span range;
};

struct WorkspaceHoverInformation final {
    WorkspaceDocumentLocation location;
    std::optional<SourceType> type;
    std::optional<SourceEntity> entity;
};

// One complete compilation result, including failed analysis and captured static
// output. Source and semantic borrows remain valid while this owner is alive.
// Client versions belong to the enclosing snapshot, not this content result.
class WorkspaceSemanticAnalysis final {
public:
    auto program() const noexcept -> const SemIRProgram*;
    auto sources() const noexcept -> const SourceManager&;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;
    auto output() const noexcept -> std::span<const CompilationOutput>;

    // Published TypeIDs belong to program(); failed analysis retains only
    // concrete builtin construction types from admitted declarations and
    // successfully constructed bodies.
    auto hover(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<WorkspaceHoverInformation>;

    auto definition(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<WorkspaceDocumentLocation>;

    // Includes declaration occurrences. Unknown targets return no result.
    auto references(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<std::vector<WorkspaceDocumentLocation>>;

    // Rename only the selected local alias, never its provided/source export.
    auto alias_references(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<std::vector<WorkspaceDocumentLocation>>;

    auto source(std::string_view document) const noexcept -> std::optional<SourceView>;

private:
    WorkspaceSemanticAnalysis(
        CompilationAnalysis analysis,
        std::map<std::string, SourceID, std::less<>> document_sources
    ) noexcept;

    auto select(std::string_view document, std::uint32_t offset) const noexcept
        -> const SourceOccurrence*;

    auto locate_source(SourceSpan location) const noexcept
        -> std::optional<WorkspaceDocumentLocation>;

    CompilationAnalysis analysis;
    std::map<std::string, SourceID, std::less<>> source_ids;
    std::map<SourceID, std::vector<SourceOccurrence>> source_occurrences;

    friend auto analyze_workspace_project(
        std::span<const WorkspaceSemanticInput> inputs,
        TimingOutput timings
    ) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
};

// Runs full compiler analysis of the selected inputs. Explicit module paths
// define imports; document identities are never interpreted as file paths.
auto analyze_workspace_project(
    std::span<const WorkspaceSemanticInput> inputs,
    TimingOutput timings = {}
) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
