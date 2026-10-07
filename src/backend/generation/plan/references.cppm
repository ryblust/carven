module carven:backend.generation.plan.references;

import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.visibility;
import std;

enum class TargetTypeCompleteness {
    Declaration,
    // The carrier layout can precede the element; its operations cannot.
    DeferredCompleteDefinition,
    CompleteDefinition,
};

// A function with static parameters is instantiated by the modules that call it,
// so its body belongs to its owner's surface: the facts include every type,
// closure and same-module function that body reaches.
struct TargetReferenceFacts final {
    std::vector<std::flat_map<NominalDeclarationRef, TargetTypeCompleteness>> surface_requirements;
    std::vector<std::flat_set<CallableID>> surface_callables;
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

auto target_nominal_dependencies(
    const SemIRProgram& semantic,
    NominalDeclarationRef nominal
) noexcept -> std::vector<NominalDeclarationRef>;
