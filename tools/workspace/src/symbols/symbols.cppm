module carven:workspace.symbols;

import :source.text;
import :workspace.document;
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

    auto operator==(const WorkspaceDocumentSymbol&) const noexcept -> bool = default;
};

using WorkspaceDocumentSymbolList = std::vector<WorkspaceDocumentSymbol>;

// Syntax declarations only; this query does not resolve names, imports, or types.
auto collect_workspace_document_symbols(const WorkspaceDocumentSyntax& document) noexcept
    -> WorkspaceDocumentSymbolList;

struct WorkspaceSymbol final {
    std::string document;
    std::string name;
    WorkspaceSymbolKind kind;
    Span range;
    Span selection;

    auto operator==(const WorkspaceSymbol&) const noexcept -> bool = default;
};

using WorkspaceSymbolList = std::vector<WorkspaceSymbol>;
