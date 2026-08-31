module carven:backend.generation.program.construction;

import :backend.generation.linkage;
import :backend.generation.program;
import :backend.generation.names;
import :semantic.hir;
import std;

enum class TargetTypeCompleteness {
    Declaration,
    CompleteDefinition,
};

struct TargetNameAllocation final {
    std::vector<TargetModuleNames> modules;
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

class TargetProgramBuilder final {
public:
    TargetProgramBuilder(SemanticProgram semantic, TargetGenerationRequest request) noexcept;

    auto finish() && noexcept -> TargetProgram;

private:
    auto allocate_names() noexcept -> void;
    auto derive_representations() noexcept -> void;
    auto derive_value_binding_requirements() noexcept -> void;
    auto build_artifact_graph() noexcept -> void;
    auto verify() const noexcept -> void;
    auto intern_carrier_shape(HIRTypeID result, FailureSetID failure_profile) noexcept
        -> TargetCarrierShapeID;

    SemanticProgram semantic;
    TargetGenerationRequest request;
    LinkageDomainID linkage_domain;
    std::optional<TargetNameAllocation> name_allocation;
    std::vector<TargetTypeRecipe> types;
    std::vector<TargetFailureProfile> failure_profiles;
    std::vector<TargetCallSignatureRecipe> call_signatures;
    std::vector<TargetCallSignatureID> callable_signatures;
    std::vector<TargetCallSignatureID> function_reference_signatures;
    std::vector<TargetCarrierShape> carrier_shapes;
    std::flat_map<std::pair<std::uint32_t, std::uint32_t>, TargetCarrierShapeID> carrier_index;
    std::vector<std::uint8_t> carrier_conversions;
    std::vector<std::uint8_t> mutable_value_binding_flags;
    std::vector<TargetArtifactSpec> artifacts;
};
