module carven:editor.symbols;

import :editor.document;
import :source.text;
import std;

enum class EditorSymbolKind { Function, Structure, Class, Enumeration, Constant, Field, EnumCase };

struct EditorDocumentSymbol final {
    std::string name;
    EditorSymbolKind kind;
    Span range;
    Span selection;
    std::vector<EditorDocumentSymbol> children;

    auto operator==(const EditorDocumentSymbol&) const noexcept -> bool = default;
};

using EditorDocumentSymbolList = std::vector<EditorDocumentSymbol>;

// Syntax declarations only; this query does not resolve names, imports, or types.
auto collect_editor_symbols(const EditorDocumentSyntax& document) noexcept
    -> EditorDocumentSymbolList;

struct EditorWorkspaceSymbol final {
    std::string document;
    std::string name;
    EditorSymbolKind kind;
    Span range;
    Span selection;

    auto operator==(const EditorWorkspaceSymbol&) const noexcept -> bool = default;
};

using EditorWorkspaceSymbolList = std::vector<EditorWorkspaceSymbol>;
