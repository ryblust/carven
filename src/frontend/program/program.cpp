module carven:frontend.program.impl;

import :frontend.program;
import :support.invariant;
import std;

SyntaxProgramParts::SyntaxProgramParts(
    CompilationProvenance provenance_value,
    std::vector<SyntaxTree> syntax_trees,
    ResolvedModuleImportGraph resolved_imports
) noexcept
    : provenance(std::move(provenance_value)),
      syntax_by_module(std::move(syntax_trees)),
      resolved_import_graph(std::move(resolved_imports)) {}

SyntaxProgram::SyntaxProgram(SyntaxProgramParts storage_value) noexcept
    : storage(std::move(storage_value)) {}

auto SyntaxProgram::syntax_tree(ProgramModuleID module_id) const noexcept -> const SyntaxTree& {
    if (!storage.provenance.view().contains(module_id)) {
        invariant_violation("syntax lookup used a foreign or invalid module identity");
    }
    return storage.syntax_by_module[module_id.index()];
}

auto SyntaxProgram::syntax_trees() const noexcept -> std::span<const SyntaxTree> {
    static_cast<void>(storage.provenance.view());
    return storage.syntax_by_module;
}

auto SyntaxProgram::resolved_imports(ProgramModuleID module_id) const noexcept
    -> std::span<const ResolvedModuleImport> {
    if (!storage.provenance.view().contains(module_id)) {
        invariant_violation("resolved import lookup used a foreign or invalid module identity");
    }
    return storage.resolved_import_graph[module_id.index()];
}

auto SyntaxProgram::resolved_import_graph() const noexcept
    -> std::span<const std::vector<ResolvedModuleImport>> {
    static_cast<void>(storage.provenance.view());
    return storage.resolved_import_graph;
}

auto SyntaxProgram::provenance() const noexcept -> CompilationProvenanceView {
    return storage.provenance.view();
}

auto SyntaxProgram::decompose() && noexcept -> SyntaxProgramParts {
    static_cast<void>(storage.provenance.view());
    return std::move(storage);
}
