module carven:analyzer.protocol;

import :diagnostics.code;
import :semantic.analysis.source;
import :workspace.analysis;
import :workspace.decorations;
import :workspace.folding;
import :workspace.symbols;
import std;

// Private pipe records; these types contain Workspace data, not a transport DOM.
enum class AnalyzerMethod : std::uint8_t {
    Hello,
    Shutdown,
    Discover,
    Workspace,
    Update,
    Close,
    Hover,
    Definition,
    References,
    Completion,
    PrepareRename,
    Rename,
    SignatureHelp,
    DocumentSymbols,
    WorkspaceSymbols,
    SemanticTokens,
    InlayHints,
    Formatting,
    CodeActions,
    DocumentHighlights,
    FoldingRanges,
    PrepareCallHierarchy,
    IncomingCalls,
    OutgoingCalls
};

struct AnalyzerTargetInput final {
    std::string name;
    bool default_target;
    std::vector<std::string> sources;
};

struct AnalyzerProjectInput final {
    std::string root;
    std::string directory;
    std::vector<std::string> roots;
    std::vector<std::string> scripts;
    std::vector<AnalyzerTargetInput> targets;
    bool available;
};

struct AnalyzerRequest final {
    AnalyzerMethod method;
    std::uint64_t revision;
    std::string uri;
    std::string text;
    std::string target;
    std::string query;
    std::string new_name;
    std::string context;
    std::int64_t version;
    bool open;
    std::uint32_t offset;
    std::optional<std::uint32_t> start;
    std::optional<std::uint32_t> end;
    bool include_declaration;
    bool quick_fix;
    bool organize_imports;
    std::vector<std::string> roots;
    std::optional<std::vector<AnalyzerProjectInput>> projects;
};

struct AnalyzerRange final {
    std::uint32_t start;
    std::uint32_t end;
};

struct AnalyzerEdit final {
    AnalyzerRange range;
    std::string replacement;
};

struct AnalyzerDocument final {
    std::string uri;
    std::string text;
    std::int64_t version;
    bool open;
};

struct AnalyzerDiagnosticRelated final {
    std::string uri;
    AnalyzerRange range;
    std::string message;
};

struct AnalyzerDiagnostic final {
    std::string uri;
    std::int64_t version;
    AnalyzerRange range;
    std::string message;
    std::string code;
    DiagnosticSeverity severity;
    std::vector<AnalyzerDiagnosticRelated> related;
    std::vector<std::string> notes;
};

struct AnalyzerWatch final {
    std::string base_uri;
    std::string pattern;
};

struct AnalyzerDocumentEdits final {
    std::string uri;
    std::string text;
    std::vector<AnalyzerEdit> edits;
};

struct AnalyzerAction final {
    std::string title;
    WorkspaceCodeActionKind kind;
    std::optional<AnalyzerDiagnostic> diagnostic;
    std::vector<AnalyzerDocumentEdits> documents;
};

struct AnalyzerToken final {
    AnalyzerRange range;
    std::uint32_t kind;
    std::uint32_t modifiers;
};

struct AnalyzerHint final {
    std::uint32_t position;
    std::string label;
    WorkspaceHintKind kind;
};

struct AnalyzerSymbol final {
    std::string name;
    WorkspaceSymbolKind kind;
    std::string uri;
    AnalyzerRange range;
    AnalyzerRange selection;
    std::vector<AnalyzerSymbol> children;
};

struct AnalyzerSignature final {
    std::string label;
    std::vector<std::string> parameters;
    std::optional<std::uint32_t> active_parameter;
};

struct AnalyzerRename final {
    AnalyzerRange range;
    std::string placeholder;
    std::vector<AnalyzerDocumentEdits> documents;
};

struct AnalyzerCompletion final {
    std::string label;
    SourceCompletionKind kind;
    std::string detail;
    std::string import_module;
    std::optional<AnalyzerEdit> import_edit;
};

struct AnalyzerLocation final {
    std::string uri;
    AnalyzerRange range;
};

struct AnalyzerHover final {
    std::string text;
    std::string uri;
    AnalyzerRange range;
    std::string documentation;
};

enum class AnalyzerQueryError { InvalidName, BindingChange, IncompleteSyntax, DocumentConflict };

struct AnalyzerQueryFailure final {
    AnalyzerQueryError code;
    std::string message;
};

struct AnalyzerHello final {
    std::uint32_t version;
};

struct AnalyzerProjects final {
    std::vector<std::string> projects;
};

struct AnalyzerWorkspace final {
    std::vector<AnalyzerDocument> documents;
    std::map<std::string, std::string, std::less<>> diagnostic_texts;
    std::vector<AnalyzerDiagnostic> diagnostics;
    std::vector<std::string> warnings;
    std::vector<AnalyzerWatch> watchers;
};

template<typename Item>
struct AnalyzerTextItems final {
    std::string text;
    std::vector<Item> items;
};

struct AnalyzerSymbols final {
    std::vector<AnalyzerSymbol> symbols;
    std::vector<AnalyzerDocument> documents;
};

struct AnalyzerCompletions final {
    AnalyzerRange replacement;
    std::vector<AnalyzerCompletion> completions;
};

struct AnalyzerLocations final {
    std::vector<AnalyzerLocation> locations;
    std::vector<AnalyzerDocument> documents;
};

struct AnalyzerCalls final {
    std::optional<WorkspaceCallable> callable;
    std::vector<WorkspaceCall> calls;
    std::vector<AnalyzerDocument> documents;
};

using AnalyzerPayload = std::variant<
    std::monostate,
    AnalyzerHello,
    AnalyzerProjects,
    AnalyzerWorkspace,
    AnalyzerTextItems<AnalyzerEdit>,
    AnalyzerTextItems<AnalyzerAction>,
    AnalyzerTextItems<AnalyzerToken>,
    AnalyzerTextItems<AnalyzerHint>,
    AnalyzerTextItems<AnalyzerRange>,
    AnalyzerTextItems<WorkspaceFold>,
    AnalyzerSymbols,
    std::optional<AnalyzerSignature>,
    std::optional<AnalyzerRename>,
    AnalyzerCompletions,
    AnalyzerLocations,
    std::optional<AnalyzerHover>,
    AnalyzerCalls>;

using AnalyzerFailure = std::variant<std::string, AnalyzerQueryFailure>;
using AnalyzerResult = std::expected<AnalyzerPayload, AnalyzerFailure>;

struct AnalyzerReply final {
    std::uint64_t revision;
    AnalyzerResult result;
};

auto decode_analyzer_request(std::span<const std::uint8_t> bytes) noexcept
    -> std::optional<AnalyzerRequest>;

auto encode_analyzer_reply(
    const AnalyzerReply& reply,
    const std::map<std::string, std::string, std::less<>>& client_uris
) noexcept -> std::vector<std::uint8_t>;
