module carven:frontend.program;

import :frontend.ast.tree;
import :source.provenance;
import :support.id_table;
import std;

class ParsedBatchConstruction;

struct ParsedBatchParts final {
    ParsedBatchParts(
        CompilationProvenance provenance,
        IDTable<SyntaxTree, ProgramModuleID> syntax_trees
    ) noexcept;
    ParsedBatchParts(const ParsedBatchParts&) = delete;
    ParsedBatchParts(ParsedBatchParts&&) = default;
    ~ParsedBatchParts() = default;

    auto operator=(const ParsedBatchParts&) -> ParsedBatchParts& = delete;
    auto operator=(ParsedBatchParts&&) -> ParsedBatchParts& = default;

    CompilationProvenance provenance;
    IDTable<SyntaxTree, ProgramModuleID> syntax_by_module;
};

class ParsedBatch final {
public:
    ParsedBatch(const ParsedBatch&) = delete;
    ParsedBatch(ParsedBatch&&) = default;
    ~ParsedBatch() = default;

    auto operator=(const ParsedBatch&) -> ParsedBatch& = delete;
    auto operator=(ParsedBatch&&) -> ParsedBatch& = default;

    auto syntax_tree(ProgramModuleID module_id) const noexcept -> const SyntaxTree&;
    auto syntax_trees() const noexcept -> std::span<const SyntaxTree>;
    auto provenance() const noexcept -> CompilationProvenanceView;
    auto decompose() && noexcept -> ParsedBatchParts;

private:
    explicit ParsedBatch(ParsedBatchParts storage) noexcept;

    ParsedBatchParts storage;

    friend class ParsedBatchConstruction;
};
