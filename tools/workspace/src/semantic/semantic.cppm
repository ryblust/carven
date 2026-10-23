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
import :support.function_ref;
import :support.timing;
import std;

struct WorkspaceProjectModule final {
    std::string document;
    CanonicalModulePath module_path;

    auto operator==(const WorkspaceProjectModule&) const noexcept -> bool = default;
};

struct WorkspaceSemanticInput final {
    WorkspaceProjectModule project_module;
    // A missing document remains an explicit input error.
    std::shared_ptr<const SourceDocument> source;
    std::shared_ptr<const ParsedDocument> parsed;
    std::vector<Span> excluded;
};

struct WorkspaceDocumentLocation final {
    std::string document;
    Span range;
};

struct WorkspaceHoverInformation final {
    WorkspaceDocumentLocation location;
    std::optional<SourceType> type;
    std::optional<SourceEntity> entity;
    std::string type_description;
    std::optional<SourceCallableInformation> callable;
    std::string description;
    std::string documentation;
};

struct WorkspaceRenameInformation final {
    WorkspaceDocumentLocation location;
    WorkspaceDocumentLocation declaration;
    std::string name;
    std::vector<WorkspaceDocumentLocation> references;
};

// One complete compilation result, including failed analysis and captured static
// output. Source and semantic borrows remain valid while this owner is alive.
// Client versions belong to the enclosing snapshot, not this content result.
class WorkspaceSemanticAnalysis final {
public:
    auto program() const noexcept -> const SemIRProgram*;
    auto source_construction_complete() const noexcept -> bool;
    auto has_source_analysis() const noexcept -> bool;
    auto sources() const noexcept -> const SourceManager&;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;
    auto output() const noexcept -> std::span<const CompilationOutput>;

    // Published TypeIDs belong to program(); failed analysis retains only
    // concrete builtin construction types and detached descriptions from admitted
    // declarations and the successfully analyzed portions of failed bodies.
    auto hover(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<WorkspaceHoverInformation>;

    auto definition(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<WorkspaceDocumentLocation>;

    // Unknown targets return no result. Declaration occurrences can be excluded.
    auto references(
        std::string_view document,
        std::uint32_t offset,
        bool include_declaration = true
    ) const noexcept -> std::optional<std::vector<WorkspaceDocumentLocation>>;

    // Rename only the selected local alias, never its provided/source export.
    auto alias_references(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<std::vector<WorkspaceDocumentLocation>>;

    auto prepare_rename(std::string_view document, std::uint32_t offset) const noexcept
        -> std::optional<WorkspaceRenameInformation>;
    // Validate recorded identities after a proposed edit, including untouched uses
    // that could otherwise be captured by the new spelling.
    auto preserves_bindings(
        const WorkspaceSemanticAnalysis& before,
        FunctionRef<Span(std::string_view, Span) noexcept> remap
    ) const noexcept -> bool;

    // Exact callee-expression evidence handles returned callables and views
    // without confusing a factory call with its returned closure's invocation.
    auto call_target(std::string_view document, Span callee) const noexcept
        -> std::optional<WorkspaceDocumentLocation>;

    auto completion_context() const noexcept -> SourceCompletionContext;
    auto completions() const noexcept -> std::span<const SourceCompletionCandidate>;

    auto source(std::string_view document) const noexcept -> std::optional<SourceView>;
    // Borrows retained source evidence; the analysis owner must remain alive.
    auto occurrences(std::string_view document) const noexcept -> std::span<const SourceOccurrence>;

private:
    WorkspaceSemanticAnalysis(
        CompilationAnalysis analysis,
        std::map<std::string, SourceID, std::less<>> document_sources
    ) noexcept;

    auto select(std::string_view document, std::uint32_t offset) const noexcept
        -> const SourceOccurrence*;

    auto locate_source(SourceSpan location) const noexcept
        -> std::optional<WorkspaceDocumentLocation>;

    friend auto analyze_workspace_source_queries(
        std::span<const WorkspaceSemanticInput> inputs,
        const std::optional<WorkspaceDocumentLocation>& completion,
        std::shared_ptr<const WorkspaceSemanticAnalysis> initial
    ) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;

    std::vector<SourceCompletionCandidate> query_completions;
    CompilationAnalysis analysis;
    std::map<std::string, SourceID, std::less<>> source_ids;
    std::map<SourceID, std::vector<SourceOccurrence>> source_occurrences;

    friend auto analyze_workspace_project(
        std::span<const WorkspaceSemanticInput> inputs,
        bool source_query,
        TimingOutput timings,
        std::optional<WorkspaceDocumentLocation> completion
    ) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
};

// An optional original result must belong to these exact inputs. Its unpublished
// semantic observation can supply the first step; completion uses its own projection.
auto analyze_workspace_source_queries(
    std::span<const WorkspaceSemanticInput> inputs,
    const std::optional<WorkspaceDocumentLocation>& completion = std::nullopt,
    std::shared_ptr<const WorkspaceSemanticAnalysis> initial = {}
) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;

// Runs full compiler analysis of the selected inputs. Explicit module paths
// define imports; document identities are never interpreted as file paths.
auto analyze_workspace_project(
    std::span<const WorkspaceSemanticInput> inputs,
    bool source_query,
    TimingOutput timings = {},
    std::optional<WorkspaceDocumentLocation> completion = std::nullopt
) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
