module carven:backend.generation.plan.references;

import :semantic.semir;
import :semantic.visibility;
import std;

enum class TargetTypeCompleteness {
    Declaration,
    CompleteDefinition,
};

struct TargetReferenceFacts final {
    std::vector<std::flat_map<NominalDeclarationRef, TargetTypeCompleteness>> surface_requirements;
    std::vector<std::flat_set<CallableID>> surface_closures;
};

auto target_visibility(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> DeclarationVisibility;
auto target_declaration_ref(NominalDeclarationRef nominal) noexcept -> DeclarationRef;
auto target_owner_module(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> ModuleID;
auto collect_target_references(
    const SemIRProgram& semantic,
    std::span<const std::vector<DeclarationRef>> surface_declarations
) noexcept -> TargetReferenceFacts;
