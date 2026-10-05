module carven:backend.lowering.context.impl;

import :backend.generation.names;
import :backend.lowering.context;
import :semantic.semir.decl;
import :source.provenance;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto domain_scope(const TargetNamePlan& names) noexcept -> TargetName {
    auto scope = names.generated_namespace();
    for (const auto& component : names.domain_namespace().components()) {
        scope.append(component);
    }
    return scope;
}

} // namespace

ArtifactLowering::ArtifactLowering(
    const PlannedCompilation& compilation,
    TargetArtifactID artifact
) noexcept
    : planned_compilation(compilation),
      artifact_id(artifact),
      cstrings(compilation.target().names(), domain_scope(compilation.target().names())) {}

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

auto ArtifactLowering::module_context(ModuleID id) noexcept -> ModuleLowering& {
    auto found = modules.find(id);
    if (found == modules.end()) {
        found = modules.emplace(id, std::make_unique<ModuleLowering>(*this, id)).first;
    }
    return *found->second;
}

auto ArtifactLowering::require_interface(ModuleID provider) noexcept -> void {
    static_cast<void>(semantic().declarations().module_decl(provider));
    const auto* implementation = std::get_if<TargetModuleImplementationArtifact>(&artifact());
    if (implementation == nullptr || provider == implementation->schedule.module_id) {
        return;
    }
    const auto interface = plan().interface_of(provider);
    if (!interface) {
        invariant_violation("cross-module lowering dependency has no provider interface");
    }
    lowering_dependencies.insert(*interface);
}

auto ArtifactLowering::take_support() noexcept -> std::vector<TargetItem> {
    auto result = cstrings.take();
    auto owners = std::vector<ModuleID>();
    for (const auto& [owner, context] : modules) {
        static_cast<void>(context);
        owners.push_back(owner);
    }
    std::ranges::sort(owners, [&](ModuleID left, ModuleID right) noexcept {
        const auto path = [&](ModuleID id) noexcept {
            return semantic()
                .provenance()
                .module_record(semantic().declarations().module_decl(id).provenance_module)
                .path.value();
        };
        return path(left) < path(right);
    });
    for (const auto owner : owners) {
        auto& context = *modules.at(owner);
        auto items = context.take_query_aliases();
        auto storage = context.constant_storage().take();
        auto helpers = context.take_display_helpers();
        items.append_range(storage | std::views::as_rvalue);
        items.append_range(helpers | std::views::as_rvalue);
        if (!items.empty()) {
            result.push_back(namespace_item(
                plan().names().module_names(owner).module_namespace_name,
                std::move(items),
                TargetCompilerReason::ArtifactScaffolding,
                false
            ));
        }
    }
    return result;
}

auto ArtifactLowering::finish(TargetUnitSections sections) && noexcept -> TargetUnit {
    if (!cstrings.empty()) {
        invariant_violation("artifact lowering did not place its C string storage");
    }
    for (const auto& [module_id, context] : modules) {
        static_cast<void>(module_id);
        if (!context->constant_storage().empty()) {
            invariant_violation("module lowering did not place its constant storage");
        }
    }
    auto dependencies =
        std::vector<TargetArtifactID>(lowering_dependencies.begin(), lowering_dependencies.end());
    auto directives = materialize_directives(plan(), artifact_id, dependencies);
    materialize_cpp_environments(sections, directives);
    return std::move(target_builder).finish(std::move(sections), std::move(directives));
}

ModuleLowering::ModuleLowering(ArtifactLowering& artifact, ModuleID owner_module_id) noexcept
    : artifact_lowering(artifact),
      module_id(owner_module_id),
      constants(
          artifact.plan().names(),
          artifact.plan().names().module_names(owner_module_id).qualified_namespace_name
      ) {
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

auto ModuleLowering::constant_storage() noexcept -> ConstantStorage& {
    return constants;
}

auto ModuleLowering::cstring_storage() noexcept -> ConstantStorage& {
    return artifact_lowering.cstrings;
}

auto ModuleLowering::active_module() const noexcept -> ModuleID {
    return module_id;
}

auto ModuleLowering::names() const noexcept -> const TargetNamePlan& {
    return plan().names();
}

auto ModuleLowering::global_function_name(FunctionID id) noexcept -> TargetName {
    const auto& function = semantic().declarations().function(id);
    const auto provider = function.module_id;
    if (provider == module_id) {
        require_callable(function.callable);
    }
    artifact_lowering.require_interface(provider);
    return names().callable_name(std::nullopt, function.callable);
}

auto ModuleLowering::structure_name(StructID id, TypeNameScope scope) noexcept -> TargetName {
    const auto provider = semantic().declarations().structure(id).module_id;
    artifact_lowering.require_interface(provider);
    return names().structure_name(
        scope == TypeNameScope::Module ? std::optional(module_id) : std::nullopt,
        id
    );
}

auto ModuleLowering::field_identifier(StructID owner, std::size_t index) const noexcept
    -> TargetIdentifier {
    const auto& field = semantic().declarations().structure(owner).fields[index];
    return source_target_identifier(
        semantic().provenance().spelling(field.name),
        names().structure_identifier(owner).spelling()
    );
}

auto ModuleLowering::enumeration_name(EnumID id, TypeNameScope scope) noexcept -> TargetName {
    const auto provider = semantic().declarations().enumeration(id).module_id;
    artifact_lowering.require_interface(provider);
    return names().enumeration_name(
        scope == TypeNameScope::Module ? std::optional(module_id) : std::nullopt,
        id
    );
}

auto ModuleLowering::callable_name(CallableID id) noexcept -> TargetName {
    const auto provider = names().callable_owner(id);
    if (provider == module_id || semantic().definition_placement(id) == DefinitionPlacement::Use) {
        require_callable(id);
    }
    artifact_lowering.require_interface(provider);
    return names().callable_name(module_id, id);
}

auto ModuleLowering::closure_type_name(CallableID id, TypeNameScope scope) noexcept -> TargetName {
    const auto provider = names().closure_owner(id);
    if (provider == module_id) {
        require_callable(id);
    }
    artifact_lowering.require_interface(provider);
    return names().closure_type_name(
        scope == TypeNameScope::Module ? std::optional(module_id) : std::nullopt,
        id
    );
}

auto ModuleLowering::require_callable(CallableID id) noexcept -> void {
    artifact_lowering.require_callable(module_id, id);
}

auto ArtifactLowering::require_callable(ModuleID owner, CallableID id) noexcept -> void {
    static_cast<void>(semantic().declarations().callable(id));
    if (semantic().definition_placement(id) == DefinitionPlacement::Use) {
        if (active_definition) {
            use_placed_edges[*active_definition].insert(id);
        }
        if (required_definitions.insert(id).second) {
            pending_definitions.push_back(id);
        }
        return;
    }
    const auto* implementation = std::get_if<TargetModuleImplementationArtifact>(&artifact());
    if (implementation == nullptr) {
        return;
    }
    if (owner != implementation->schedule.module_id) {
        if (!std::ranges::contains(plan().module_schedule(owner).interface_callables, id)) {
            invariant_violation("shared body reaches an unpublished callable");
        }
        return;
    }
    if (required_definitions.insert(id).second) {
        pending_definitions.push_back(id);
    }
}

auto ArtifactLowering::use_placed_callees(CallableID id) const noexcept
    -> const std::set<CallableID>& {
    static const auto none = std::set<CallableID>();
    const auto found = use_placed_edges.find(id);
    return found == use_placed_edges.end() ? none : found->second;
}

auto ArtifactLowering::next_definition() noexcept -> std::optional<CallableID> {
    active_definition.reset();
    if (pending_definitions.empty()) {
        return std::nullopt;
    }
    active_definition = pending_definitions.front();
    pending_definitions.pop_front();
    return active_definition;
}

auto ModuleLowering::payload_enum(EnumID id) noexcept -> const TargetPayloadEnumNames& {
    const auto provider = semantic().declarations().enumeration(id).module_id;
    artifact_lowering.require_interface(provider);
    return names().payload_enum(id);
}

auto ModuleLowering::make_callable_name_allocator() const noexcept -> TargetNameAllocator {
    return TargetNameAllocator(plan().names().module_names(module_id).body_reserved_identifiers);
}

auto ModuleLowering::take_query_aliases() noexcept -> std::vector<TargetItem> {
    return std::exchange(query_aliases, {});
}
