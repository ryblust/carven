module carven:backend.generation.plan.references;

import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.visibility;
import std;

using TargetTypeDefinitionRef = std::variant<StructID, EnumID, CallableID>;

enum class TargetTypeCompleteness {
    Declaration,
    // The carrier layout can precede the element; its operations cannot.
    DeferredCompleteDefinition,
    CompleteDefinition,
};

// Parameterized definitions are instantiated by their callers, so concrete
// dependencies in their checked bodies belong to their owner's target surface.
// Private source declarations retain their original source visibility.
struct TargetReferenceFacts final {
    std::vector<std::flat_map<TargetTypeDefinitionRef, TargetTypeCompleteness>>
        surface_requirements;
    std::vector<std::flat_set<CallableID>> surface_callables;
    std::flat_set<NominalDeclarationRef> host_nominals;
};

auto target_visibility(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> DeclarationVisibility;

auto target_declaration_ref(NominalDeclarationRef nominal) noexcept -> DeclarationRef;

auto target_owner_module(const SemIRProgram& semantic, DeclarationRef declaration) noexcept
    -> ModuleID;

auto collect_target_references(
    const SemIRProgram& semantic,
    std::span<const std::vector<DeclarationRef>> surface_declarations,
    std::span<const FunctionID> providers
) noexcept -> TargetReferenceFacts;

auto target_type_owner(const SemIRProgram& semantic, TargetTypeDefinitionRef definition) noexcept
    -> ModuleID;

auto target_type_definition_order(
    const SemIRProgram& semantic,
    std::span<const CallableID> closures
) noexcept -> std::vector<TargetTypeDefinitionRef>;
