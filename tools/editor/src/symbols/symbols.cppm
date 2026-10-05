module carven:editor.symbols;

import :editor.document;
import :source.text;
import std;

namespace editor {

enum class SymbolKind { Function, Structure, Class, Enumeration, Constant, Field, EnumCase };

struct DocumentSymbol final {
    std::string name;
    SymbolKind kind;
    Span range;
    Span selection;
    std::vector<DocumentSymbol> children;

    auto operator==(const DocumentSymbol&) const noexcept -> bool = default;
};

using DocumentSymbolList = std::vector<DocumentSymbol>;

// Syntax declarations only; this query does not resolve names, imports, or types.
auto collect_symbols(const DocumentSyntax& document) noexcept -> DocumentSymbolList;

struct WorkspaceSymbol final {
    std::string document;
    std::string name;
    SymbolKind kind;
    Span range;
    Span selection;

    auto operator==(const WorkspaceSymbol&) const noexcept -> bool = default;
};

using WorkspaceSymbolList = std::vector<WorkspaceSymbol>;

} // namespace editor
