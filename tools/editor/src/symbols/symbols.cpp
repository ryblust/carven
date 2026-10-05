module carven:editor.symbols.impl;

import :editor.document;
import :editor.symbols;
import :frontend.ast.decl;
import :frontend.ast.ids;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :source.text;
import :support.visit;
import std;

namespace {

class SymbolCollector final {
public:
    SymbolCollector(ASTView syntax, std::string_view source) noexcept;
    auto collect() const noexcept -> editor::DocumentSymbolList;

private:
    auto collect_item(ASTItemID id) const noexcept -> std::optional<editor::DocumentSymbol>;
    auto symbol(editor::SymbolKind kind, Span range, Span name) const noexcept
        -> editor::DocumentSymbol;

    ASTView syntax;
    std::string_view source;
};

SymbolCollector::SymbolCollector(ASTView syntax, std::string_view source) noexcept
    : syntax(syntax),
      source(source) {}

auto SymbolCollector::symbol(editor::SymbolKind kind, Span range, Span name) const noexcept
    -> editor::DocumentSymbol {
    return editor::DocumentSymbol {
        .name = std::string(slice(source, name)),
        .kind = kind,
        .range = range,
        .selection = name,
        .children = {},
    };
}

auto SymbolCollector::collect_item(ASTItemID id) const noexcept
    -> std::optional<editor::DocumentSymbol> {
    const auto& item = syntax.item(id);
    return item.value.visit(
        Overloaded {
            [&](const ASTFunctionDecl& declaration) noexcept
                -> std::optional<editor::DocumentSymbol> {
                if (declaration.is_implicit_entry) {
                    return std::nullopt;
                }
                return symbol(editor::SymbolKind::Function, item.span, declaration.name_span);
            },
            [&](const ASTRecordDecl& declaration) noexcept
                -> std::optional<editor::DocumentSymbol> {
                auto result = symbol(
                    declaration.kind == ASTRecordKind::Class ? editor::SymbolKind::Class
                                                             : editor::SymbolKind::Structure,
                    item.span,
                    declaration.name_span
                );
                for (const auto& field : declaration.fields) {
                    result.children.push_back(
                        symbol(editor::SymbolKind::Field, field.span, field.name_span)
                    );
                }
                for (const auto operation : declaration.operations) {
                    if (auto child = collect_item(operation)) {
                        result.children.push_back(std::move(*child));
                    }
                }
                return result;
            },
            [&](const ASTEnumDecl& declaration) noexcept -> std::optional<editor::DocumentSymbol> {
                auto result =
                    symbol(editor::SymbolKind::Enumeration, item.span, declaration.name_span);
                for (const auto& entry : declaration.cases) {
                    result.children.push_back(
                        symbol(editor::SymbolKind::EnumCase, entry.span, entry.name_span)
                    );
                }
                return result;
            },
            [&](const ASTConstantDecl& declaration) noexcept
                -> std::optional<editor::DocumentSymbol> {
                return symbol(editor::SymbolKind::Constant, item.span, declaration.name_span);
            },
            [](const ASTConstBlock&) static noexcept -> std::optional<editor::DocumentSymbol> {
                return std::nullopt;
            },
            [](const ASTTestDecl&) static noexcept -> std::optional<editor::DocumentSymbol> {
                return std::nullopt;
            },
        }
    );
}

auto SymbolCollector::collect() const noexcept -> editor::DocumentSymbolList {
    auto result = editor::DocumentSymbolList();
    for (const auto id : syntax.ast_module().items) {
        if (auto declaration = collect_item(id)) {
            result.push_back(std::move(*declaration));
        }
    }
    return result;
}

} // namespace

namespace editor {

auto collect_symbols(const DocumentSyntax& document) noexcept -> DocumentSymbolList {
    const auto syntax = document.recovered_syntax();
    if (!syntax) {
        return {};
    }
    return SymbolCollector(*syntax, document.source().text).collect();
}

} // namespace editor
