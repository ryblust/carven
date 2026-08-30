module carven:backend.generation.plan;

import :backend.generation.linkage;
import :backend.generation.names;
import :backend.target.name;
import :semantic.hir;
import :semantic.hir.ids;
import std;

struct TargetEntityName final {
    ProgramModuleID owner_module;
    TargetName relative_name;
};

struct TargetFailureSetProfile final {
    std::vector<HIRTypeID> ordered_members;
};

struct TargetModuleGenerationPlan final {
    std::vector<HIRDeclarationRef> surface_declarations;
    std::vector<HIRNominalDeclRef> implementation_nominal_order;
    std::string implementation_logical_path;
    TargetName qualified_namespace_name;
    TargetName module_namespace_name;
    std::vector<std::string> interface_header_paths;
    std::flat_set<std::string> reserved_identifiers;
};

struct TargetHeaderDeclarationPlan final {
    ProgramModuleID module_id;
    HIRDeclarationRef declaration;
};

struct TargetHeaderForwardDeclarationPlan final {
    ProgramModuleID module_id;
    HIRNominalDeclRef declaration;
};

struct TargetInterfaceComponentPlan final {
    std::string logical_path;
    std::vector<std::string> prerequisite_header_paths;
    std::vector<TargetHeaderForwardDeclarationPlan> forward_declarations;
    std::vector<TargetHeaderDeclarationPlan> declarations;
};

class TargetGenerationPlan final {
public:
    static auto build(
        const SemanticProgram& semantic,
        const LinkageDomainID& linkage_domain
    ) noexcept -> TargetGenerationPlan;

    auto module_plan(ProgramModuleID module_id) const noexcept -> const TargetModuleGenerationPlan&;
    auto read_parameter_by_value(HIRTypeID type_id) const noexcept -> bool;
    auto failure_set(FailureSetID failure_set_id) const noexcept -> const TargetFailureSetProfile&;
    auto requires_mutable_value_binding(SemanticPlaceID place_id) const noexcept -> bool;
    auto generated_namespace() const noexcept -> const TargetName&;
    auto domain_namespace() const noexcept -> const TargetName&;
    auto entity_identifier(SymbolID symbol_id) const noexcept -> const TargetIdentifier&;
    auto entity_name(ProgramModuleID active_module, SymbolID symbol_id) const noexcept
        -> TargetName;
    auto payload_enum(EnumID enum_id) const noexcept -> const TargetPayloadEnumNames&;
    auto source_names(SemanticScopeID scope_id) const noexcept -> const std::flat_set<std::string>&;
    auto interface_components() const noexcept -> std::span<const TargetInterfaceComponentPlan>;

private:
    friend class TargetPlanConstruction;

    TargetGenerationPlan(
        std::vector<TargetModuleGenerationPlan> modules,
        std::vector<std::uint8_t> read_parameter_by_value_flags,
        std::vector<TargetFailureSetProfile> failure_sets,
        std::vector<std::uint8_t> mutable_value_binding_flags,
        TargetName generated_namespace,
        TargetName domain_namespace,
        std::vector<std::optional<TargetEntityName>> entity_names,
        std::vector<std::optional<TargetPayloadEnumNames>> payload_enums,
        std::vector<std::flat_set<std::string>> scoped_source_names,
        std::vector<TargetInterfaceComponentPlan> interface_components
    ) noexcept;

    auto resolve_name(ProgramModuleID active_module, const TargetEntityName& name) const noexcept
        -> TargetName;

    std::vector<TargetModuleGenerationPlan> module_plans;
    std::vector<std::uint8_t> read_parameter_by_value_flags;
    std::vector<TargetFailureSetProfile> failure_set_profiles;
    std::vector<std::uint8_t> mutable_value_binding_flags;
    TargetName target_generated_namespace;
    TargetName target_domain_namespace;
    std::vector<std::optional<TargetEntityName>> target_entity_names;
    std::vector<std::optional<TargetPayloadEnumNames>> target_payload_enums;
    std::vector<std::flat_set<std::string>> target_scoped_source_names;
    std::vector<TargetInterfaceComponentPlan> interface_component_plans;
};
