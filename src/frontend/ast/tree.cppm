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
};

class ASTBuilder;

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

    friend class ASTBuilder;
};
