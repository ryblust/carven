module carven:workspace.symbols.impl;

import :frontend.ast.decl;
import :frontend.ast.ids;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :source.text;
import :support.visit;
import :workspace.document;
import :workspace.symbols;
import std;

namespace {

class SymbolCollector final {
public:
    SymbolCollector(ASTView syntax, std::string_view source) noexcept;
    auto collect() const noexcept -> WorkspaceDocumentSymbolList;

private:
    auto collect_item(ASTItemID id) const noexcept -> std::optional<WorkspaceDocumentSymbol>;
    auto symbol(WorkspaceSymbolKind kind, Span range, Span name) const noexcept
        -> WorkspaceDocumentSymbol;

    ASTView syntax;
    std::string_view source;
};

SymbolCollector::SymbolCollector(ASTView syntax, std::string_view source) noexcept
    : syntax(syntax),
      source(source) {}

auto SymbolCollector::symbol(WorkspaceSymbolKind kind, Span range, Span name) const noexcept
    -> WorkspaceDocumentSymbol {
    return WorkspaceDocumentSymbol {
        .name = std::string(slice(source, name)),
        .kind = kind,
        .range = range,
        .selection = name,
        .children = {},
    };
}

auto SymbolCollector::collect_item(ASTItemID id) const noexcept
    -> std::optional<WorkspaceDocumentSymbol> {
    const auto& item = syntax.item(id);
    return item.value.visit(
        Overloaded {
            [&](const ASTFunctionDecl& declaration) noexcept
                -> std::optional<WorkspaceDocumentSymbol> {
                if (declaration.is_implicit_entry) {
                    return std::nullopt;
                }
                return symbol(WorkspaceSymbolKind::Function, item.span, declaration.name_span);
            },
            [&](const ASTRecordDecl& declaration) noexcept
                -> std::optional<WorkspaceDocumentSymbol> {
                auto result = symbol(
                    declaration.kind == ASTRecordKind::Class ? WorkspaceSymbolKind::Class
                                                             : WorkspaceSymbolKind::Structure,
                    item.span,
                    declaration.name_span
                );
                for (const auto& field : declaration.fields) {
                    result.children.push_back(
                        symbol(WorkspaceSymbolKind::Field, field.span, field.name_span)
                    );
                }
                for (const auto operation : declaration.operations) {
                    if (auto child = collect_item(operation)) {
                        result.children.push_back(std::move(*child));
                    }
                }
                return result;
            },
            [&](const ASTEnumDecl& declaration) noexcept -> std::optional<WorkspaceDocumentSymbol> {
                auto result =
                    symbol(WorkspaceSymbolKind::Enumeration, item.span, declaration.name_span);
                for (const auto& entry : declaration.cases) {
                    result.children.push_back(
                        symbol(WorkspaceSymbolKind::EnumCase, entry.span, entry.name_span)
                    );
                }
                return result;
            },
            [&](const ASTConstantDecl& declaration) noexcept
                -> std::optional<WorkspaceDocumentSymbol> {
                return symbol(WorkspaceSymbolKind::Constant, item.span, declaration.name_span);
            },
            [](const ASTConstBlock&) static noexcept -> std::optional<WorkspaceDocumentSymbol> {
                return std::nullopt;
            },
            [](const ASTTestDecl&) static noexcept -> std::optional<WorkspaceDocumentSymbol> {
                return std::nullopt;
            },
        }
    );
}

auto SymbolCollector::collect() const noexcept -> WorkspaceDocumentSymbolList {
    auto result = WorkspaceDocumentSymbolList();
    for (const auto id : syntax.ast_module().items) {
        if (auto declaration = collect_item(id)) {
            result.push_back(std::move(*declaration));
        }
    }
    return result;
}

} // namespace

auto collect_workspace_document_symbols(const WorkspaceDocumentSyntax& document) noexcept
    -> WorkspaceDocumentSymbolList {
    const auto syntax = document.recovered_syntax();
    if (!syntax) {
        return {};
    }
    return SymbolCollector(*syntax, document.source().text).collect();
}
