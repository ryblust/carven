module carven:frontend.program.impl;

import :frontend.program;
import std;

ParsedBatchParts::ParsedBatchParts(
    CompilationProvenance provenance_value,
    IDTable<SyntaxTree, ProgramModuleID> syntax_trees
) noexcept
    : provenance(std::move(provenance_value)),
      syntax_by_module(std::move(syntax_trees)) {}

ParsedBatch::ParsedBatch(ParsedBatchParts storage_value) noexcept
    : storage(std::move(storage_value)) {}

auto ParsedBatch::syntax_tree(ProgramModuleID module_id) const noexcept -> const SyntaxTree& {
    return storage.syntax_by_module.get(module_id);
}

auto ParsedBatch::syntax_trees() const noexcept -> std::span<const SyntaxTree> {
    return storage.syntax_by_module.values();
}

auto ParsedBatch::provenance() const noexcept -> CompilationProvenanceView {
    return storage.provenance.view();
}

auto ParsedBatch::decompose() && noexcept -> ParsedBatchParts {
    return std::move(storage);
}
