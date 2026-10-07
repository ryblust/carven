module carven:workspace.analysis;

import :source.text;
import :support.timing;
import :workspace.document;
import :workspace.semantic;
import :workspace.symbols;
import std;

class WorkspaceQueries;

struct WorkspaceQueryCounts final {
    std::size_t syntax;
    std::size_t document_symbols;
    std::size_t workspace_symbols;
    std::size_t semantic;
};

struct WorkspaceSyntaxQuery final {
    std::int64_t version;
    std::shared_ptr<const WorkspaceDocumentSyntax> result;
};

struct WorkspaceSymbolsQuery final {
    WorkspaceSyntaxQuery document;
    // Null means unrecoverable syntax. Complete or recovered trees may have
    // empty lists; inspect document.result for diagnostics and completeness.
    std::shared_ptr<const WorkspaceDocumentSymbolList> result;
};

struct WorkspaceDocumentVersion final {
    std::string document;
    std::int64_t version;
};

struct WorkspaceSemanticQuery final {
    // Semantic IDs and source spans belong to this owning result.
    // Only hover/definition targets outside the project have no analysis owner.
    std::shared_ptr<const WorkspaceSemanticAnalysis> result;
    // Content results can be reused while client versions advance.
    std::vector<WorkspaceDocumentVersion> documents;
};

struct WorkspaceHoverQuery final {
    WorkspaceSemanticQuery analysis;
    std::optional<WorkspaceHoverInformation> result;
};

struct WorkspaceVersionedLocation final {
    std::string document;
    std::int64_t version;
    Span range;
};

struct WorkspaceDefinitionQuery final {
    WorkspaceSemanticQuery analysis;
    std::optional<WorkspaceVersionedLocation> result;
};

struct WorkspaceReferencesQuery final {
    WorkspaceSemanticQuery analysis;
    std::optional<std::vector<WorkspaceVersionedLocation>> result;
};

// Snapshots retain their input versions, source owners, and lazily cached results.
// Host updates and lazy queries must be serialized.
class WorkspaceAnalysisSnapshot final {
public:
    auto syntax(std::string_view document) const noexcept -> std::optional<WorkspaceSyntaxQuery>;
    auto document_symbols(std::string_view document) const noexcept
        -> std::optional<WorkspaceSymbolsQuery>;
    auto workspace_symbols() const noexcept -> std::shared_ptr<const WorkspaceSymbolList>;
    // The explicit module set is the semantic dependency boundary.
    // An uncached selection runs full compiler analysis.
    // Timings are delivered only for a new computation and borrowed for this call.
    auto semantic(
        std::span<const WorkspaceProjectModule> project,
        TimingOutput timings = {}
    ) const noexcept -> WorkspaceSemanticQuery;
    // Source types and resolved references share one occurrence index. Failed
    // analysis can retain builtin types and definitions from admitted declarations
    // and successfully constructed bodies.
    // Hover and definition analyze the document's transitive import closure.
    // Invalid project mappings are rejected by the compiler input boundary.
    // Targets outside the project return no information or analysis owner.
    // Returned document versions describe the actual analyzed module selection.
    // Offsets and ranges are UTF-8 byte positions; queries use half-open ranges.
    auto hover(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceHoverQuery;
    auto definition(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceDefinitionQuery;
    // Includes declaration occurrences in the explicit selected module set.
    auto references(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceReferencesQuery;
    // Counts are cumulative across the host and its retained snapshots.
    auto counts() const noexcept -> WorkspaceQueryCounts;

private:
    explicit WorkspaceAnalysisSnapshot(std::shared_ptr<WorkspaceQueries> queries) noexcept;

    std::shared_ptr<WorkspaceQueries> queries;

    friend class WorkspaceAnalysisHost;
};

enum class WorkspaceDocumentChange { Added, Changed, VersionOnly };

struct WorkspaceStaleDocumentVersion final {
    std::int64_t current;
    std::int64_t received;
};

using WorkspaceDocumentUpdateFailure = std::variant<WorkspaceStaleDocumentVersion, SourceLoadError>;

class WorkspaceAnalysisHost final {
public:
    WorkspaceAnalysisHost() noexcept;
    WorkspaceAnalysisHost(const WorkspaceAnalysisHost&) = delete;
    WorkspaceAnalysisHost(WorkspaceAnalysisHost&&) = default;
    auto operator=(const WorkspaceAnalysisHost&) -> WorkspaceAnalysisHost& = delete;
    auto operator=(WorkspaceAnalysisHost&&) -> WorkspaceAnalysisHost& = delete;
    // Document keys are caller-supplied identities; the host performs no filesystem IO.
    auto update(std::string document, std::int64_t version, std::string text) noexcept
        -> std::expected<WorkspaceDocumentChange, WorkspaceDocumentUpdateFailure>;
    auto remove(std::string_view document) noexcept -> bool;
    auto snapshot() const noexcept -> WorkspaceAnalysisSnapshot;

private:
    std::shared_ptr<WorkspaceQueries> queries;
};
