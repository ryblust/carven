module carven:workspace.symbols;

import :frontend.document;
import :source.text;
import std;

enum class WorkspaceSymbolKind {
    Function,
    Structure,
    Class,
    Enumeration,
    Constant,
    Field,
    EnumCase
};

struct WorkspaceDocumentSymbol final {
    std::string name;
    WorkspaceSymbolKind kind;
    Span range;
    Span selection;
    std::vector<WorkspaceDocumentSymbol> children;
};

using WorkspaceDocumentSymbolList = std::vector<WorkspaceDocumentSymbol>;

// Syntax declarations only; this query does not resolve names, imports, or types.
auto collect_workspace_document_symbols(const ParsedDocument& document) noexcept
    -> WorkspaceDocumentSymbolList;

struct WorkspaceSymbol final {
    std::string document;
    std::string name;
    WorkspaceSymbolKind kind;
    Span range;
    Span selection;
};

using WorkspaceSymbolList = std::vector<WorkspaceSymbol>;
