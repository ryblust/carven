module carven:frontend.ast.tree;

import :frontend.ast.storage;
import :source.text;

class SyntaxTree final {
public:
    SyntaxTree(const SyntaxTree&) = delete;
    SyntaxTree(SyntaxTree&&) = default;
    auto operator=(const SyntaxTree&) -> SyntaxTree& = delete;
    auto operator=(SyntaxTree&&) -> SyntaxTree& = default;

    auto view() const noexcept -> ASTView;

private:
    SyntaxTree(ASTStorage storage, ASTModule ast_module, SourceID source) noexcept;

    ASTStorage ast_storage;
    ASTModule source_module;
    SourceID source_identity;

    friend class Parser;
};
