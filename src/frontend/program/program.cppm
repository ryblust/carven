module carven:frontend.program;

import :frontend.ast.ids;
import :frontend.ast.tree;
import :source.provenance;
import std;

class SyntaxProgramBuilder;

struct ResolvedModuleImport final {
    ASTModuleImportID declaration;
    ProgramModuleID target;
};

using ResolvedModuleImportGraph = std::vector<std::vector<ResolvedModuleImport>>;

struct SyntaxProgramParts final {
    SyntaxProgramParts(
        CompilationProvenance provenance,
        std::vector<SyntaxTree> syntax_trees,
        ResolvedModuleImportGraph resolved_imports
    ) noexcept;
    SyntaxProgramParts(const SyntaxProgramParts&) = delete;
    SyntaxProgramParts(SyntaxProgramParts&&) = default;
    ~SyntaxProgramParts() = default;
    auto operator=(const SyntaxProgramParts&) -> SyntaxProgramParts& = delete;
    auto operator=(SyntaxProgramParts&&) -> SyntaxProgramParts& = delete;

    CompilationProvenance provenance;
    std::vector<SyntaxTree> syntax_by_module;
    ResolvedModuleImportGraph resolved_import_graph;
};

class SyntaxProgram final {
public:
    SyntaxProgram(const SyntaxProgram&) = delete;
    SyntaxProgram(SyntaxProgram&&) = default;
    ~SyntaxProgram() = default;
    auto operator=(const SyntaxProgram&) -> SyntaxProgram& = delete;
    auto operator=(SyntaxProgram&&) -> SyntaxProgram& = delete;
    auto syntax_tree(ProgramModuleID id) const noexcept -> const SyntaxTree&;
    auto syntax_trees() const noexcept -> std::span<const SyntaxTree>;
    auto resolved_imports(ProgramModuleID id) const noexcept
        -> std::span<const ResolvedModuleImport>;
    auto resolved_import_graph() const noexcept
        -> std::span<const std::vector<ResolvedModuleImport>>;
    auto provenance() const noexcept -> CompilationProvenanceView;
    auto decompose() && noexcept -> SyntaxProgramParts;

private:
    explicit SyntaxProgram(SyntaxProgramParts storage) noexcept;

    SyntaxProgramParts storage;

    friend class SyntaxProgramBuilder;
};
