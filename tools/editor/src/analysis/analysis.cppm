module carven:editor.analysis;

import :editor.document;
import :editor.semantic;
import :editor.symbols;
import :source.text;
import std;

class EditorWorkspaceQueries;

struct EditorQueryCounts final {
    std::size_t syntax;
    std::size_t document_symbols;
    std::size_t workspace_symbols;
    std::size_t semantic;
};

struct EditorSyntaxQuery final {
    std::int64_t version;
    std::shared_ptr<const EditorDocumentSyntax> result;
};

struct EditorSymbolsQuery final {
    EditorSyntaxQuery document;
    // Null means unrecoverable syntax. Complete or recovered trees may have
    // empty lists; inspect document.result for diagnostics and completeness.
    std::shared_ptr<const EditorDocumentSymbolList> result;
};

struct EditorDocumentVersion final {
    std::string document;
    std::int64_t version;
};

struct EditorSemanticQuery final {
    // Semantic IDs and source spans belong to this owning result.
    std::shared_ptr<const EditorSemanticAnalysis> result;
    // Content results can be reused while client versions advance.
    std::vector<EditorDocumentVersion> documents;
};

struct EditorHoverQuery final {
    EditorSemanticQuery analysis;
    std::optional<EditorHoverInformation> result;
};

struct EditorVersionedLocation final {
    std::string document;
    std::int64_t version;
    Span range;
};

struct EditorDefinitionQuery final {
    EditorSemanticQuery analysis;
    std::optional<EditorVersionedLocation> result;
};

struct EditorReferencesQuery final {
    EditorSemanticQuery analysis;
    std::optional<std::vector<EditorVersionedLocation>> result;
};

// Snapshots retain their input versions, source owners, and lazily cached results.
// Host updates and queries are serialized; this prototype has no thread safety.
class EditorAnalysis final {
public:
    auto syntax(std::string_view document) const noexcept -> std::optional<EditorSyntaxQuery>;
    auto document_symbols(std::string_view document) const noexcept
        -> std::optional<EditorSymbolsQuery>;
    auto workspace_symbols() const noexcept -> std::shared_ptr<const EditorWorkspaceSymbolList>;
    // The explicit module set is the semantic dependency boundary. Its provider
    // currently performs full analysis when any selected input changes.
    auto semantic(std::span<const EditorProjectModule> project) const noexcept
        -> EditorSemanticQuery;
    // Source types and resolved references share one occurrence index. Failed
    // analysis can retain builtin types and definitions from completed bodies.
    // Offsets and ranges are UTF-8 byte positions; queries use half-open ranges.
    auto hover(
        std::span<const EditorProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> EditorHoverQuery;
    auto definition(
        std::span<const EditorProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> EditorDefinitionQuery;
    // Includes declaration occurrences in the explicit selected module set.
    auto references(
        std::span<const EditorProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> EditorReferencesQuery;
    // Counts are cumulative across the host and its retained snapshots.
    auto counts() const noexcept -> EditorQueryCounts;

private:
    explicit EditorAnalysis(std::shared_ptr<EditorWorkspaceQueries> queries) noexcept;

    std::shared_ptr<EditorWorkspaceQueries> queries;

    friend class EditorAnalysisHost;
};

enum class EditorDocumentChange { Added, Changed, VersionOnly };

struct EditorStaleDocumentVersion final {
    std::int64_t current;
    std::int64_t received;
};

using EditorDocumentUpdateFailure = std::variant<EditorStaleDocumentVersion, SourceLoadError>;

class EditorAnalysisHost final {
public:
    EditorAnalysisHost() noexcept;
    EditorAnalysisHost(const EditorAnalysisHost&) = delete;
    EditorAnalysisHost(EditorAnalysisHost&&) = default;
    auto operator=(const EditorAnalysisHost&) -> EditorAnalysisHost& = delete;
    auto operator=(EditorAnalysisHost&&) -> EditorAnalysisHost& = delete;
    // Document keys are caller-supplied identities; the host performs no filesystem IO.
    auto update(std::string document, std::int64_t version, std::string text) noexcept
        -> std::expected<EditorDocumentChange, EditorDocumentUpdateFailure>;
    auto remove(std::string_view document) noexcept -> bool;
    auto snapshot() const noexcept -> EditorAnalysis;

private:
    std::shared_ptr<EditorWorkspaceQueries> queries;
};
