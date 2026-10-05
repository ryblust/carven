module carven:editor.analysis;

import :editor.document;
import :editor.semantic;
import :editor.symbols;
import :source.text;
import std;

namespace editor {

class WorkspaceQueries;

struct QueryCounts final {
    std::size_t syntax;
    std::size_t document_symbols;
    std::size_t workspace_symbols;
    std::size_t semantic;
};

struct SyntaxQuery final {
    std::int64_t version;
    std::shared_ptr<const DocumentSyntax> result;
};

struct SymbolsQuery final {
    SyntaxQuery document;
    // Null means unrecoverable syntax. Complete or recovered trees may have
    // empty lists; inspect document.result for diagnostics and completeness.
    std::shared_ptr<const DocumentSymbolList> result;
};

struct DocumentVersion final {
    std::string document;
    std::int64_t version;
};

struct SemanticQuery final {
    // Semantic IDs and source spans belong to this owning result.
    std::shared_ptr<const SemanticAnalysis> result;
    // Content results can be reused while client versions advance.
    std::vector<DocumentVersion> documents;
};

struct HoverQuery final {
    SemanticQuery analysis;
    std::optional<HoverInformation> result;
};

struct VersionedLocation final {
    std::string document;
    std::int64_t version;
    Span range;
};

struct DefinitionQuery final {
    SemanticQuery analysis;
    std::optional<VersionedLocation> result;
};

struct ReferencesQuery final {
    SemanticQuery analysis;
    std::optional<std::vector<VersionedLocation>> result;
};

// Snapshots retain their input versions, source owners, and lazily cached results.
// Host updates and queries are serialized; this prototype has no thread safety.
class Analysis final {
public:
    auto syntax(std::string_view document) const noexcept -> std::optional<SyntaxQuery>;
    auto document_symbols(std::string_view document) const noexcept -> std::optional<SymbolsQuery>;
    auto workspace_symbols() const noexcept -> std::shared_ptr<const WorkspaceSymbolList>;
    // The explicit module set is the semantic dependency boundary. Its provider
    // currently performs full analysis when any selected input changes.
    auto semantic(std::span<const ProjectModule> project) const noexcept -> SemanticQuery;
    // Source types and resolved references share one occurrence index. Failed
    // analysis can retain builtin types and definitions from completed bodies.
    // Offsets and ranges are UTF-8 byte positions; queries use half-open ranges.
    auto hover(
        std::span<const ProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> HoverQuery;
    auto definition(
        std::span<const ProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> DefinitionQuery;
    // Includes declaration occurrences in the explicit selected module set.
    auto references(
        std::span<const ProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> ReferencesQuery;
    // Counts are cumulative across the host and its retained snapshots.
    auto counts() const noexcept -> QueryCounts;

private:
    explicit Analysis(std::shared_ptr<WorkspaceQueries> queries) noexcept;

    std::shared_ptr<WorkspaceQueries> queries;

    friend class AnalysisHost;
};

enum class DocumentChange { Added, Changed, VersionOnly };

struct StaleDocumentVersion final {
    std::int64_t current;
    std::int64_t received;
};

using DocumentUpdateFailure = std::variant<StaleDocumentVersion, SourceLoadError>;

class AnalysisHost final {
public:
    AnalysisHost() noexcept;
    AnalysisHost(const AnalysisHost&) = delete;
    AnalysisHost(AnalysisHost&&) = default;
    auto operator=(const AnalysisHost&) -> AnalysisHost& = delete;
    auto operator=(AnalysisHost&&) -> AnalysisHost& = delete;
    // Document keys are caller-supplied identities; the host performs no filesystem IO.
    auto update(std::string document, std::int64_t version, std::string text) noexcept
        -> std::expected<DocumentChange, DocumentUpdateFailure>;
    auto remove(std::string_view document) noexcept -> bool;
    auto snapshot() const noexcept -> Analysis;

private:
    std::shared_ptr<WorkspaceQueries> queries;
};

} // namespace editor
