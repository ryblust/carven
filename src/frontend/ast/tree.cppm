module carven:frontend.ast.tree;

import :frontend.ast.ids;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :source.text;
import std;

struct ASTModule final {
    Span span;
    std::vector<ASTModuleImportID> module_imports;
    std::vector<ASTCppHeaderImport> cpp_header_imports;
    std::vector<ASTCppSourceFragment> cpp_source_fragments;
    std::vector<ASTItemID> items;
    // Known names from declarations that could not retain a complete AST item.
    std::vector<Span> unavailable_names;
    // False when recovery cannot determine all names introduced into this scope.
    bool scope_complete;
};

class ASTBuilder;

class SyntaxTree final {
public:
    SyntaxTree(const SyntaxTree&) = delete;
    SyntaxTree(SyntaxTree&&) = default;
    auto operator=(const SyntaxTree&) -> SyntaxTree& = delete;
    auto operator=(SyntaxTree&&) -> SyntaxTree& = default;
    auto bind(SourceID source) const noexcept -> SyntaxTree;
    auto view() const noexcept -> ASTView;

private:
    SyntaxTree(ASTStorage storage, ASTModule ast_module, SourceID source) noexcept;

    struct Storage final {
        ASTStorage ast;
        ASTModule ast_module;
    };

    SyntaxTree(std::shared_ptr<const Storage> storage, SourceID source) noexcept;

    std::shared_ptr<const Storage> storage;
    SourceID source_identity;

    friend class ASTBuilder;
};
