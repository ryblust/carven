module carven:backend.generation.plan.impl;

import :backend.generation.plan;
import :backend.generation.plan.construction;
import :support.invariant;
import std;

namespace {

auto logical_path(
    std::string_view prefix,
    std::span<const std::string> components,
    std::string_view extension
) noexcept -> std::string {
    auto result = std::string(prefix);
    for (const auto& component : components) {
        if (!result.empty()) {
            result += '/';
        }
        result += component;
    }
    result += extension;
    return result;
}

} // namespace

auto module_implementation_logical_path(std::span<const std::string> canonical_components) noexcept
    -> std::string {
    return logical_path({}, canonical_components, ".cpp");
}

auto interface_component_logical_path(std::span<const std::string> anchor_components) noexcept
    -> std::string {
    return logical_path("carven/generated", anchor_components, ".hpp");
}

TargetPlanConstruction::TargetPlanConstruction(
    const SemanticProgram& semantic,
    const LinkageDomainID& linkage_domain
) noexcept
    : semantic(semantic),
      linkage_domain(linkage_domain) {}

auto TargetPlanConstruction::finish() && noexcept -> TargetGenerationPlan {
    allocate_names();
    derive_representations();
    derive_value_binding_requirements();
    plan_references_and_interfaces();
    if (!names.has_value()) {
        invariant_violation("target plan names were not completed");
    }
    return TargetGenerationPlan(
        std::move(names->modules),
        std::move(read_parameter_by_value_flags),
        std::move(failure_sets),
        std::move(mutable_value_binding_flags),
        std::move(names->generated_namespace),
        std::move(names->domain_namespace),
        std::move(names->entities),
        std::move(names->payload_enums),
        std::move(names->scoped_source_names),
        std::move(interface_components)
    );
}

TargetGenerationPlan::TargetGenerationPlan(
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
) noexcept
    : module_plans(std::move(modules)),
      read_parameter_by_value_flags(std::move(read_parameter_by_value_flags)),
      failure_set_profiles(std::move(failure_sets)),
      mutable_value_binding_flags(std::move(mutable_value_binding_flags)),
      target_generated_namespace(std::move(generated_namespace)),
      target_domain_namespace(std::move(domain_namespace)),
      target_entity_names(std::move(entity_names)),
      target_payload_enums(std::move(payload_enums)),
      target_scoped_source_names(std::move(scoped_source_names)),
      interface_component_plans(std::move(interface_components)) {}

auto TargetGenerationPlan::module_plan(ProgramModuleID module_id) const noexcept
    -> const TargetModuleGenerationPlan& {
    if (module_id.index() >= module_plans.size()) {
        invariant_violation("target generation plan references an unknown module");
    }
    return module_plans[module_id.index()];
}

auto TargetGenerationPlan::read_parameter_by_value(HIRTypeID type_id) const noexcept -> bool {
    if (type_id.index() >= read_parameter_by_value_flags.size()) {
        invariant_violation("target generation plan references an unknown type");
    }
    return read_parameter_by_value_flags[type_id.index()] != 0u;
}

auto TargetGenerationPlan::failure_set(FailureSetID failure_set_id) const noexcept
    -> const TargetFailureSetProfile& {
    if (failure_set_id.index() >= failure_set_profiles.size()) {
        invariant_violation("target generation plan references an unknown failure set");
    }
    return failure_set_profiles[failure_set_id.index()];
}

auto TargetGenerationPlan::requires_mutable_value_binding(SemanticPlaceID place_id) const noexcept
    -> bool {
    if (place_id.index() >= mutable_value_binding_flags.size()) {
        invariant_violation("target generation plan references an unknown semantic place");
    }
    return mutable_value_binding_flags[place_id.index()] != 0u;
}

auto TargetGenerationPlan::generated_namespace() const noexcept -> const TargetName& {
    return target_generated_namespace;
}

auto TargetGenerationPlan::domain_namespace() const noexcept -> const TargetName& {
    return target_domain_namespace;
}

auto TargetGenerationPlan::entity_identifier(SymbolID symbol_id) const noexcept
    -> const TargetIdentifier& {
    if (symbol_id.index() >= target_entity_names.size()
        || !target_entity_names[symbol_id.index()].has_value()) {
        invariant_violation("target generation plan references an unnamed symbol");
    }
    return target_entity_names[symbol_id.index()]->relative_name.components().back();
}

auto TargetGenerationPlan::resolve_name(
    ProgramModuleID active_module,
    const TargetEntityName& name
) const noexcept -> TargetName {
    auto components = std::vector<TargetIdentifier>(
        name.relative_name.components().begin(),
        name.relative_name.components().end()
    );
    if (name.owner_module == active_module) {
        return TargetName::from_components(std::move(components));
    }
    const auto& target_namespace =
        module_plan(name.owner_module).qualified_namespace_name.components();
    auto qualified =
        std::vector<TargetIdentifier>(target_namespace.begin(), target_namespace.end());
    qualified.insert(qualified.end(), components.begin(), components.end());
    return TargetName::globally_qualified(std::move(qualified));
}

auto TargetGenerationPlan::entity_name(
    ProgramModuleID active_module,
    SymbolID symbol_id
) const noexcept -> TargetName {
    if (symbol_id.index() >= target_entity_names.size()
        || !target_entity_names[symbol_id.index()].has_value()) {
        invariant_violation("target generation plan references an unnamed symbol");
    }
    return resolve_name(active_module, *target_entity_names[symbol_id.index()]);
}

auto TargetGenerationPlan::payload_enum(EnumID enum_id) const noexcept
    -> const TargetPayloadEnumNames& {
    if (enum_id.index() >= target_payload_enums.size()
        || !target_payload_enums[enum_id.index()].has_value()) {
        invariant_violation("target generation plan references a non-payload enum");
    }
    return *target_payload_enums[enum_id.index()];
}

auto TargetGenerationPlan::source_names(SemanticScopeID scope_id) const noexcept
    -> const std::flat_set<std::string>& {
    if (scope_id.index() >= target_scoped_source_names.size()) {
        invariant_violation("target generation plan references an unknown semantic scope");
    }
    return target_scoped_source_names[scope_id.index()];
}

auto TargetGenerationPlan::interface_components() const noexcept
    -> std::span<const TargetInterfaceComponentPlan> {
    return interface_component_plans;
}

auto TargetGenerationPlan::build(
    const SemanticProgram& semantic,
    const LinkageDomainID& linkage_domain
) noexcept -> TargetGenerationPlan {
    return TargetPlanConstruction(semantic, linkage_domain).finish();
}
