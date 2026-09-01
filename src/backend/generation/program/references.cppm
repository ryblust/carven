module carven:backend.generation.program.references;

import :semantic.hir;
import :semantic.visibility;
import std;

enum class TargetTypeCompleteness {
    Declaration,
    CompleteDefinition,
};

struct TargetReferenceFacts final {
    struct SurfaceDeclarationReferences final {
        HIRDeclarationRef declaration;
        std::flat_map<HIRNominalDeclRef, TargetTypeCompleteness> requirements;
    };

    std::vector<std::flat_map<HIRNominalDeclRef, TargetTypeCompleteness>> surface_requirements;
    std::vector<std::vector<SurfaceDeclarationReferences>> surface_declarations;
    std::vector<std::flat_set<ProgramModuleID>> implementation_dependencies;
};

auto target_visibility(const SemanticProgram& semantic, HIRDeclarationRef declaration) noexcept
    -> DeclarationVisibility;

auto target_declaration_ref(HIRNominalDeclRef nominal) noexcept -> HIRDeclarationRef;

auto target_owner_module(const SemanticProgram& semantic, HIRDeclarationRef declaration) noexcept
    -> ProgramModuleID;

auto collect_target_references(
    const SemanticProgram& semantic,
    std::span<const std::vector<HIRDeclarationRef>> surface_declarations
) noexcept -> TargetReferenceFacts;
