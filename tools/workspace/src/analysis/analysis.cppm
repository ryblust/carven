module carven:workspace.analysis;

import :diagnostics.diagnostic;
import :frontend.document;
import :semantic.analysis.source;
import :source.text;
import :support.timing;
import :workspace.decorations;
import :workspace.folding;
import :workspace.semantic;
import :workspace.symbols;
import std;

class WorkspaceQueries;

struct WorkspaceSyntaxQuery final {
    std::int64_t version;
    std::shared_ptr<const ParsedDocument> result;
};

struct WorkspaceSymbolsQuery final {
    WorkspaceSyntaxQuery document;
    // Null means unrecoverable syntax. Complete or recovered trees may have
    // empty lists; inspect document.result for diagnostics and completeness.
    std::shared_ptr<const WorkspaceDocumentSymbolList> result;
};

struct WorkspaceFoldingQuery final {
    WorkspaceSyntaxQuery document;
    std::shared_ptr<const WorkspaceFoldList> result;
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

struct WorkspaceHighlightsQuery final {
    WorkspaceSemanticQuery analysis;
    std::vector<Span> result;
};

struct WorkspaceTextEdit final {
    std::string document;
    Span range;
    std::string replacement;
};

struct WorkspaceAutoImport final {
    SourceCompletionCandidate candidate;
    std::string module_path;
    WorkspaceTextEdit edit;
};

struct WorkspaceCompletionQuery final {
    Span replacement;
    std::shared_ptr<const WorkspaceSemanticAnalysis> analysis;
    std::vector<SourceCompletionCandidate> candidates;
    std::vector<WorkspaceAutoImport> imports;
};

struct WorkspaceRenameQuery final {
    WorkspaceSemanticQuery analysis;
    std::optional<WorkspaceRenameInformation> result;
};

enum class WorkspaceRenameError { InvalidName, Conflict };

struct WorkspaceSignatureInformation final {
    std::string label;
    std::vector<std::string> parameters;
    std::optional<std::uint32_t> active_parameter;
};

struct WorkspaceSignatureQuery final {
    WorkspaceSemanticQuery analysis;
    std::optional<WorkspaceSignatureInformation> result;
};

struct WorkspaceCallable final {
    WorkspaceDocumentLocation location;
    Span range;
    std::string name;
    std::string detail;
    bool method;
};

struct WorkspaceCall final {
    WorkspaceCallable callable;
    // Call sites always belong to the caller's document.
    std::vector<Span> ranges;
};

struct WorkspaceCallHierarchyQuery final {
    WorkspaceSemanticQuery analysis;
    std::optional<WorkspaceCallable> item;
    std::vector<WorkspaceCall> calls;
};

enum class WorkspaceCallDirection { Incoming, Outgoing };

struct WorkspaceFormattingQuery final {
    WorkspaceSyntaxQuery document;
    std::expected<std::optional<WorkspaceTextEdit>, Diagnostics> result;
};

enum class WorkspaceCodeActionKind { QuickFix, OrganizeImports };

struct WorkspaceCodeAction final {
    std::string title;
    std::optional<DiagnosticFinding> finding;
    Span diagnostic_range;
    std::vector<WorkspaceTextEdit> edits;
    WorkspaceCodeActionKind kind;
};

struct WorkspaceCodeActionsQuery final {
    WorkspaceSemanticQuery analysis;
    std::vector<WorkspaceCodeAction> result;
};

struct WorkspaceDecorationsQuery final {
    WorkspaceSemanticQuery analysis;
    WorkspaceDecorations result;
};

// Snapshots retain their input versions, source owners, and lazily cached results.
// Host updates and lazy queries must be serialized.
class WorkspaceAnalysisSnapshot final {
public:
    auto syntax(std::string_view document) const noexcept -> std::optional<WorkspaceSyntaxQuery>;

    // Formats complete cached syntax with the canonical Formatter style.
    // Invalid syntax yields diagnostics; unchanged text yields no edit.
    auto formatting(std::string_view document) const noexcept
        -> std::optional<WorkspaceFormattingQuery>;

    // Only compiler-authored fixes from the current strict diagnostic selection.
    auto code_actions(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        Span visible,
        bool quick_fixes = true,
        bool organize_imports = false
    ) const noexcept -> WorkspaceCodeActionsQuery;

    auto auto_imports(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        bool types_only,
        std::span<const SourceCompletionCandidate> visible = {}
    ) const noexcept -> std::vector<WorkspaceAutoImport>;
    auto organize_imports(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        const WorkspaceSemanticAnalysis& analysis
    ) const noexcept -> std::vector<WorkspaceTextEdit>;

    auto document_symbols(std::string_view document) const noexcept
        -> std::optional<WorkspaceSymbolsQuery>;

    auto folding_ranges(std::string_view document) const noexcept
        -> std::optional<WorkspaceFoldingQuery>;

    // Current-document references from the shared navigation/recovery analysis.
    // Import aliases retain their local binding identity; no spelling matches.
    auto document_highlights(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceHighlightsQuery;

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
    // and successfully constructed bodies. Source-query recovery can retain detached
    // evidence beyond failed statements without publishing a program.
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

    // References cover the explicit selected module set, including reverse imports.
    auto references(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset,
        bool include_declaration = true
    ) const noexcept -> WorkspaceReferencesQuery;

    // A cached private completion selection admits unfinished names and braces.
    // It never updates the snapshot or publishes its synthetic diagnostics.
    auto completion(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceCompletionQuery;

    auto prepare_rename(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceRenameQuery;
    auto rename(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset,
        std::string_view new_name
    ) const noexcept -> std::expected<WorkspaceRenameQuery, WorkspaceRenameError>;
    auto decorations(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        Span visible
    ) const noexcept -> WorkspaceDecorationsQuery;

    auto signature_help(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceSignatureQuery;

    // Functions, closures and statically proven function values use compiler
    // identity. Signature-only dynamic views never supply a guessed target.
    auto prepare_call_hierarchy(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceCallHierarchyQuery;
    // Expansion offsets are declaration anchors returned by the prepared item
    // or a previous edge, including the zero-width implicit entry anchor.
    auto incoming_calls(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceCallHierarchyQuery;
    auto outgoing_calls(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset
    ) const noexcept -> WorkspaceCallHierarchyQuery;

private:
    auto source_semantic(std::span<const WorkspaceProjectModule> project) const noexcept
        -> WorkspaceSemanticQuery;
    auto call_hierarchy(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document,
        std::uint32_t offset,
        std::optional<WorkspaceCallDirection> direction
    ) const noexcept -> WorkspaceCallHierarchyQuery;

    auto interop_hover(
        const WorkspaceSemanticAnalysis& analysis,
        std::string_view document,
        std::uint32_t offset,
        std::optional<WorkspaceHoverInformation> hover
    ) const noexcept -> std::optional<WorkspaceHoverInformation>;

    // Shared source/semantic signature labels for hover and call help.
    auto callable_signature(
        const WorkspaceSemanticAnalysis& analysis,
        const WorkspaceHoverInformation& hover,
        bool declaration
    ) const noexcept -> std::optional<WorkspaceSignatureInformation>;

    explicit WorkspaceAnalysisSnapshot(std::shared_ptr<WorkspaceQueries> queries) noexcept;

    std::shared_ptr<WorkspaceQueries> queries;

    friend class WorkspaceAnalysisHost;
};

enum class WorkspaceDocumentChange { Added, Changed, VersionOnly };

struct WorkspaceDocumentUpdate final {
    std::string document;
    std::int64_t version;
    std::string text;
};

struct WorkspaceStaleDocumentVersion final {
    std::int64_t current;
    std::int64_t received;
};

struct WorkspaceDuplicateDocumentUpdate final {};

using WorkspaceDocumentUpdateFailure =
    std::variant<WorkspaceStaleDocumentVersion, SourceLoadError, WorkspaceDuplicateDocumentUpdate>;

struct WorkspaceDocumentBatchUpdateFailure final {
    std::string document;
    WorkspaceDocumentUpdateFailure reason;
};

class WorkspaceAnalysisHost final {
public:
    // Zero disables retained semantic selections; returned query owners stay valid.
    explicit WorkspaceAnalysisHost(std::size_t semantic_cache_capacity) noexcept;
    WorkspaceAnalysisHost(const WorkspaceAnalysisHost&) = delete;
    WorkspaceAnalysisHost(WorkspaceAnalysisHost&&) = default;
    auto operator=(const WorkspaceAnalysisHost&) -> WorkspaceAnalysisHost& = delete;
    auto operator=(WorkspaceAnalysisHost&&) -> WorkspaceAnalysisHost& = delete;

    // Document keys are caller-supplied identities; the host performs no filesystem IO.
    auto update(std::string document, std::int64_t version, std::string text) noexcept
        -> std::expected<WorkspaceDocumentChange, WorkspaceDocumentUpdateFailure>;

    // Publish all updates together. Duplicate keys and stale versions reject the
    // entire batch; successful changes follow input order. Empty batches do nothing.
    auto update(std::vector<WorkspaceDocumentUpdate> updates) noexcept
        -> std::expected<std::vector<WorkspaceDocumentChange>, WorkspaceDocumentBatchUpdateFailure>;

    auto remove(std::string_view document) noexcept -> bool;
    auto snapshot() const noexcept -> WorkspaceAnalysisSnapshot;

private:
    std::shared_ptr<WorkspaceQueries> queries;
};
