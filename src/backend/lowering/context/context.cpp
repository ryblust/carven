module carven:backend.lowering.context.impl;

import :backend.lowering.context;
import :support.invariant;
import std;

ArtifactLowering::ArtifactLowering(
    const PlannedCompilation& compilation,
    TargetArtifactID artifact
) noexcept
    : planned_compilation(compilation),
      artifact_id(artifact) {
    static_cast<void>(compilation.target().artifact(artifact));
}

auto ArtifactLowering::semantic() const noexcept -> const SemIRProgram& {
    return planned_compilation.semantic();
}

auto ArtifactLowering::plan() const noexcept -> const TargetPlan& {
    return planned_compilation.target();
}

auto ArtifactLowering::artifact() const noexcept -> const TargetArtifactPlan& {
    return plan().artifact(artifact_id);
}

auto ArtifactLowering::target() noexcept -> TargetUnitBuilder& {
    return target_builder;
}

auto ArtifactLowering::module_context(ModuleID id) noexcept -> ModuleLowering {
    return ModuleLowering(*this, id);
}

auto ArtifactLowering::record_provider_interface(ModuleID active, ModuleID provider) noexcept
    -> void {
    if (active.owner() != semantic().identity() || provider.owner() != semantic().identity()) {
        invariant_violation("target lowering dependency used a foreign module");
    }
    if (active == provider
        || !std::holds_alternative<TargetModuleImplementationArtifact>(artifact())) {
        return;
    }
    for (const auto entry : plan().artifacts()) {
        const auto* interface = std::get_if<TargetInterfaceArtifact>(&entry.value);
        if (interface != nullptr && std::ranges::contains(interface->component_members, provider)) {
            lowering_dependencies.insert(entry.id);
            return;
        }
    }
    invariant_violation("cross-module lowering dependency has no provider interface");
}

auto ArtifactLowering::finish(TargetUnitSections sections) && noexcept -> TargetUnit {
    auto dependencies =
        std::vector<TargetArtifactID>(lowering_dependencies.begin(), lowering_dependencies.end());
    auto directives = materialize_directives(plan(), artifact_id, dependencies);
    materialize_cpp_environments(sections, directives);
    return std::move(target_builder).finish(std::move(sections), std::move(directives));
}

ModuleLowering::ModuleLowering(ArtifactLowering& artifact, ModuleID owner_module_id) noexcept
    : artifact_lowering(artifact),
      module_id(owner_module_id),
      allocator(artifact.plan().names().module_names(owner_module_id).reserved_identifiers) {
    if (owner_module_id.owner() != semantic().identity()) {
        invariant_violation("module lowering received a foreign semantic module ID");
    }
    static_cast<void>(semantic().declarations().module_decl(owner_module_id));
}

auto ModuleLowering::semantic() const noexcept -> const SemIRProgram& {
    return artifact_lowering.semantic();
}

auto ModuleLowering::plan() const noexcept -> const TargetPlan& {
    return artifact_lowering.plan();
}

auto ModuleLowering::target() noexcept -> TargetUnitBuilder& {
    return artifact_lowering.target();
}

auto ModuleLowering::active_module() const noexcept -> ModuleID {
    return module_id;
}

auto ModuleLowering::names() const noexcept -> const TargetNamePlan& {
    return plan().names();
}

auto ModuleLowering::global_function_name(FunctionID id) noexcept -> TargetName {
    const auto provider = semantic().declarations().function(id).module_id;
    artifact_lowering.record_provider_interface(module_id, provider);
    return names().global_function_name(id);
}

auto ModuleLowering::structure_name(StructID id) noexcept -> TargetName {
    const auto provider = semantic().declarations().structure(id).module_id;
    artifact_lowering.record_provider_interface(module_id, provider);
    return names().structure_name(module_id, id);
}

auto ModuleLowering::enumeration_name(EnumID id) noexcept -> TargetName {
    const auto provider = semantic().declarations().enumeration(id).module_id;
    artifact_lowering.record_provider_interface(module_id, provider);
    return names().enumeration_name(module_id, id);
}

auto ModuleLowering::callable_name(CallableID id) noexcept -> TargetName {
    artifact_lowering.record_provider_interface(module_id, names().callable_owner(id));
    return names().callable_name(module_id, id);
}

auto ModuleLowering::closure_type_name(CallableID id) noexcept -> TargetName {
    artifact_lowering.record_provider_interface(module_id, names().closure_owner(id));
    return names().closure_type_name(module_id, id);
}

auto ModuleLowering::payload_enum(EnumID id) noexcept -> const TargetPayloadEnumNames& {
    const auto provider = semantic().declarations().enumeration(id).module_id;
    artifact_lowering.record_provider_interface(module_id, provider);
    return names().payload_enum(id);
}

auto ModuleLowering::name_allocator() noexcept -> TargetNameAllocator& {
    return allocator;
}

auto ModuleLowering::make_callable_name_allocator() const noexcept -> TargetNameAllocator {
    return TargetNameAllocator(plan().names().module_names(module_id).reserved_identifiers);
}
