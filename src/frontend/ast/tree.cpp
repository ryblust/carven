module carven:frontend.ast.tree.impl;

import :frontend.ast.storage;
import :frontend.ast.tree;
import :source.text;
import std;

SyntaxTree::SyntaxTree(ASTStorage storage, ASTModule ast_module, SourceID source) noexcept
    : ast_storage(std::move(storage)),
      source_module(std::move(ast_module)),
      source_identity(source) {}

auto SyntaxTree::view() const noexcept -> ASTView {
    return ASTView(ast_storage, source_module, source_identity);
}
