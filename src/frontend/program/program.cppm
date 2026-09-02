module carven:frontend.program;

import :frontend.ast.tree;
import :source.provenance;
import :support.id_table;
import std;

class SyntaxProgramBuilder;

struct SyntaxProgramParts final {
    SyntaxProgramParts(
        CompilationProvenance provenance,
        IDTable<SyntaxTree, ProgramModuleID> syntax_trees
    ) noexcept;
    SyntaxProgramParts(const SyntaxProgramParts&) = delete;
    SyntaxProgramParts(SyntaxProgramParts&&) = default;
    ~SyntaxProgramParts() = default;

    auto operator=(const SyntaxProgramParts&) -> SyntaxProgramParts& = delete;
    auto operator=(SyntaxProgramParts&&) -> SyntaxProgramParts& = default;

    CompilationProvenance provenance;
    IDTable<SyntaxTree, ProgramModuleID> syntax_by_module;
};

class SyntaxProgram final {
public:
    SyntaxProgram(const SyntaxProgram&) = delete;
    SyntaxProgram(SyntaxProgram&&) = default;
    ~SyntaxProgram() = default;

    auto operator=(const SyntaxProgram&) -> SyntaxProgram& = delete;
    auto operator=(SyntaxProgram&&) -> SyntaxProgram& = default;

    auto syntax_tree(ProgramModuleID module_id) const noexcept -> const SyntaxTree&;
    auto syntax_trees() const noexcept -> std::span<const SyntaxTree>;
    auto provenance() const noexcept -> CompilationProvenanceView;
    auto decompose() && noexcept -> SyntaxProgramParts;

private:
    explicit SyntaxProgram(SyntaxProgramParts storage) noexcept;

    SyntaxProgramParts storage;

    friend class SyntaxProgramBuilder;
};
