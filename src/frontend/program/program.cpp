module carven:frontend.program.impl;

import :frontend.program;
import std;

SyntaxProgramParts::SyntaxProgramParts(
    CompilationProvenance provenance_value,
    IDTable<SyntaxTree, ProgramModuleID> syntax_trees
) noexcept
    : provenance(std::move(provenance_value)),
      syntax_by_module(std::move(syntax_trees)) {}

SyntaxProgram::SyntaxProgram(SyntaxProgramParts storage_value) noexcept
    : storage(std::move(storage_value)) {}

auto SyntaxProgram::syntax_tree(ProgramModuleID module_id) const noexcept -> const SyntaxTree& {
    return storage.syntax_by_module.get(module_id);
}

auto SyntaxProgram::syntax_trees() const noexcept -> std::span<const SyntaxTree> {
    return storage.syntax_by_module.values();
}

auto SyntaxProgram::provenance() const noexcept -> CompilationProvenanceView {
    return storage.provenance.view();
}

auto SyntaxProgram::decompose() && noexcept -> SyntaxProgramParts {
    return std::move(storage);
}
