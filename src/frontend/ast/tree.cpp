module carven:frontend.ast.tree.impl;

import :frontend.ast.storage;
import :frontend.ast.tree;
import :source.text;
import std;

SyntaxTree::SyntaxTree(ASTStorage storage, ASTModule ast_module, SourceID source) noexcept
    : storage(
          std::make_shared<const Storage>(
              Storage {.ast = std::move(storage), .ast_module = std::move(ast_module)}
          )
      ),
      source_identity(source) {}

auto SyntaxTree::view() const noexcept -> ASTView {
    return ASTView(storage->ast, storage->ast_module, source_identity);
}

SyntaxTree::SyntaxTree(std::shared_ptr<const Storage> storage, SourceID source) noexcept
    : storage(std::move(storage)),
      source_identity(source) {}

auto SyntaxTree::bind(SourceID source) const noexcept -> SyntaxTree {
    return SyntaxTree(storage, source);
}
