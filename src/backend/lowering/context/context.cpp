module carven:backend.lowering.context.impl;

import :backend.lowering.context;
import :support.invariant;
import :support.visit;
import std;

ArtifactLowering::ArtifactLowering(
    const PlannedCompilation& compilation,
    TargetArtifactID artifact
) noexcept
    : planned_compilation(std::addressof(compilation)),
      artifact_id(artifact) {
    static_cast<void>(compilation.target().artifact(artifact));
}

ArtifactLowering::ArtifactLowering(ArtifactLowering&& other) noexcept
    : planned_compilation(std::exchange(other.planned_compilation, nullptr)),
      artifact_id(other.artifact_id),
      target_builder(std::move(other.target_builder)),
      lowering_dependencies(std::move(other.lowering_dependencies)),
      cpp_type_providers(std::move(other.cpp_type_providers)) {}

auto ArtifactLowering::require_compilation() const noexcept -> const PlannedCompilation& {
    if (planned_compilation == nullptr) {
        invariant_violation("artifact lowering context was used after move");
    }
    return *planned_compilation;
}

auto ArtifactLowering::semantic() const noexcept -> const SemIRProgram& {
    return require_compilation().semantic();
}

auto ArtifactLowering::plan() const noexcept -> const TargetPlan& {
    return require_compilation().target();
}

auto ArtifactLowering::artifact() const noexcept -> const TargetArtifactPlan& {
    return plan().artifact(artifact_id);
}

auto ArtifactLowering::target() noexcept -> TargetUnitBuilder& {
    static_cast<void>(require_compilation());
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
    auto imports = std::vector<TargetItem>();
    for (const auto& [provider, names] : cpp_type_providers) {
        const auto needed = [&](const CppUsingBinding& binding) noexcept {
            return binding.opens_namespace
                || names.contains(semantic().provenance().spelling(binding.components.back()));
        };
        auto bindings = std::vector<TargetItem>();
        for (const auto& header : semantic().declarations().module_decl(provider).cpp_headers) {
            if (!std::ranges::any_of(header.bindings, needed)) {
                continue;
            }
            const auto name = semantic().provenance().spelling(header.name);
            directives.prefix_groups.push_back(
                {.directives =
                     {{.bytes = header.delimiter == CppHeaderDelimiter::AngleBrackets
                           ? std::format("#include <{}>", name)
                           : std::format("#include \"{}\"", name)}},
                 .attribution = std::nullopt}
            );
            for (const auto& binding : header.bindings) {
                if (!needed(binding)) {
                    continue;
                }
                auto components = std::vector<TargetIdentifier>();
                for (const auto component : binding.components) {
                    components.push_back(
                        TargetIdentifier::from_spelling(semantic().provenance().spelling(component))
                    );
                }
                bindings.push_back(source_item(
                    semantic(),
                    binding.origin,
                    TargetUsing {
                        .name = TargetName::globally_qualified(std::move(components)),
                        .opens_namespace = binding.opens_namespace
                    }
                ));
            }
        }
        imports.push_back(namespace_item(
            plan().names().module_names(provider).qualified_namespace_name,
            std::move(bindings)
        ));
    }
    sections.preamble.insert(
        sections.preamble.begin(),
        std::make_move_iterator(imports.begin()),
        std::make_move_iterator(imports.end())
    );
    return std::move(target_builder).finish(std::move(sections), std::move(directives));
}

ModuleLowering::ModuleLowering(ArtifactLowering& artifact, ModuleID owner_module_id) noexcept
    : artifact_lowering(std::addressof(artifact)),
      module_id(owner_module_id),
      type_states(artifact.semantic().types().size(), LoweringState::Unseen),
      type_cache(artifact.semantic().types().size()),
      signature_states(artifact.semantic().callable_signatures().size(), LoweringState::Unseen),
      signature_result_cache(artifact.semantic().callable_signatures().size()) {
    if (owner_module_id.owner() != semantic().identity()) {
        invariant_violation("module lowering received a foreign semantic module ID");
    }
    static_cast<void>(semantic().declarations().module_decl(owner_module_id));
    for (const auto& name : plan().names().module_names(owner_module_id).reserved_identifiers) {
        allocator.reserve(name);
    }
}

ModuleLowering::ModuleLowering(ModuleLowering&& other) noexcept
    : artifact_lowering(std::exchange(other.artifact_lowering, nullptr)),
      module_id(other.module_id),
      allocator(std::move(other.allocator)),
      type_states(std::move(other.type_states)),
      type_cache(std::move(other.type_cache)),
      signature_states(std::move(other.signature_states)),
      signature_result_cache(std::move(other.signature_result_cache)) {}

auto ModuleLowering::require_artifact() const noexcept -> ArtifactLowering& {
    if (artifact_lowering == nullptr) {
        invariant_violation("module lowering context was used after move");
    }
    return *artifact_lowering;
}

auto ModuleLowering::semantic() const noexcept -> const SemIRProgram& {
    return require_artifact().semantic();
}

auto ModuleLowering::plan() const noexcept -> const TargetPlan& {
    return require_artifact().plan();
}

auto ModuleLowering::target() noexcept -> TargetUnitBuilder& {
    return require_artifact().target();
}

auto ModuleLowering::active_module() const noexcept -> ModuleID {
    static_cast<void>(require_artifact());
    return module_id;
}

auto ModuleLowering::names() const noexcept -> const TargetNamePlan& {
    return plan().names();
}

auto ModuleLowering::global_function_name(FunctionID id) noexcept -> TargetName {
    const auto provider = semantic().declarations().function(id).module_id;
    require_artifact().record_provider_interface(module_id, provider);
    return names().global_function_name(id);
}

auto ModuleLowering::structure_name(StructID id) noexcept -> TargetName {
    const auto provider = semantic().declarations().structure(id).module_id;
    require_artifact().record_provider_interface(module_id, provider);
    return names().structure_name(module_id, id);
}

auto ModuleLowering::enumeration_name(EnumID id) noexcept -> TargetName {
    const auto provider = semantic().declarations().enumeration(id).module_id;
    require_artifact().record_provider_interface(module_id, provider);
    return names().enumeration_name(module_id, id);
}

auto ModuleLowering::callable_name(CallableID id) noexcept -> TargetName {
    require_artifact().record_provider_interface(module_id, names().callable_owner(id));
    return names().callable_name(module_id, id);
}

auto ModuleLowering::closure_type_name(CallableID id) noexcept -> TargetName {
    require_artifact().record_provider_interface(module_id, names().closure_owner(id));
    return names().closure_type_name(module_id, id);
}

auto ModuleLowering::payload_enum(EnumID id) noexcept -> const TargetPayloadEnumNames& {
    const auto provider = semantic().declarations().enumeration(id).module_id;
    require_artifact().record_provider_interface(module_id, provider);
    return names().payload_enum(id);
}

auto ModuleLowering::name_allocator() noexcept -> TargetNameAllocator& {
    static_cast<void>(require_artifact());
    return allocator;
}

auto ModuleLowering::make_callable_name_allocator() const noexcept -> TargetNameAllocator {
    auto result = TargetNameAllocator {};
    for (const auto& name : plan().names().module_names(module_id).reserved_identifiers) {
        result.reserve(name);
    }
    return result;
}
