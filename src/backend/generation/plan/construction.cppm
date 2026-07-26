module carven:backend.generation.plan.construction;

import :backend.generation.linkage;
import :backend.generation.names;
import :backend.generation.plan;
import :semantic.hir;
import std;

enum class TargetTypeCompleteness {
    Declaration,
    CompleteDefinition,
};

struct TargetNamePlan final {
    std::vector<TargetModuleGenerationPlan> modules;
    TargetName generated_namespace;
    TargetName domain_namespace;
    std::vector<std::optional<TargetEntityName>> entities;
    std::vector<std::optional<TargetPayloadEnumNames>> payload_enums;
    std::vector<std::flat_set<std::string>> scoped_source_names;
};

auto module_implementation_logical_path(std::span<const std::string> canonical_components) noexcept
    -> std::string;
auto interface_component_logical_path(std::span<const std::string> anchor_components) noexcept
    -> std::string;

class TargetPlanConstruction final {
public:
    TargetPlanConstruction(
        const SemanticProgram& semantic,
        const TargetDomainID& linkage_domain
    ) noexcept;

    auto finish() && noexcept -> TargetGenerationPlan;

private:
    auto allocate_names() noexcept -> void;
    auto derive_representations() noexcept -> void;
    auto derive_value_binding_requirements() noexcept -> void;
    auto plan_references_and_interfaces() noexcept -> void;

    const SemanticProgram& semantic;
    const TargetDomainID& linkage_domain;
    std::optional<TargetNamePlan> names;
    std::vector<std::uint8_t> read_parameter_by_value_flags;
    std::vector<TargetFailureSetProfile> failure_sets;
    std::vector<std::uint8_t> mutable_value_binding_flags;
    std::vector<TargetInterfaceComponentPlan> interface_components;
};
