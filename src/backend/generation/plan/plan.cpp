module carven:backend.generation.plan.impl;

import :artifacts;
import :backend.generation.plan;
import :backend.generation.request;
import :support.invariant;
import :support.visit;
import std;

namespace {

template<typename Value, typename ID>
auto semantic_row(
    const std::vector<Value>& rows,
    ProgramIdentity owner,
    ID id,
    std::string_view fact
) noexcept -> const Value& {
    if (id.owner() != owner || id.index() >= rows.size()) {
        invariant_violation(fact);
    }
    return rows[id.index()];
}

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

auto target_source_origin(CompilationProvenanceView provenance, ProgramOriginID origin) noexcept
    -> TargetSourceOrigin {
    const auto span = provenance.source_span(origin);
    const auto source = provenance.find_source_snapshot(span.source_id);
    if (!source.has_value()) {
        invariant_violation("semantic origin references an unknown source snapshot");
    }
    return {
        .display_origin = std::string(provenance.source_snapshot(*source).display_origin()),
        .line = provenance.location(origin).line,
    };
}

auto TargetClosureCatalog::owner(CallableID callable) const noexcept -> ModuleID {
    const auto& result = semantic_row(
        owner_modules,
        semantic_identity,
        callable,
        "target closure catalog used an unknown callable"
    );
    if (!result.has_value()) {
        invariant_violation("target closure catalog used a non-closure callable");
    }
    return *result;
}

auto TargetClosureCatalog::production(ModuleID module) const noexcept
    -> std::span<const CallableID> {
    return semantic_row(
        production_definitions,
        semantic_identity,
        module,
        "target closure catalog used an unknown module"
    );
}

auto TargetClosureCatalog::tests(ModuleID module) const noexcept -> std::span<const CallableID> {
    return semantic_row(
        test_definitions,
        semantic_identity,
        module,
        "target closure catalog used an unknown module"
    );
}

TargetNamePlan::TargetNamePlan(
    ProgramIdentity semantic_identity,
    std::vector<TargetModuleNames> modules,
    TargetName generated_namespace,
    TargetName domain_namespace,
    std::vector<TargetEntityName> functions,
    std::vector<TargetEntityName> structures,
    std::vector<TargetEntityName> enumerations,
    std::vector<std::optional<TargetEntityName>> callables,
    std::vector<std::optional<TargetEntityName>> closure_types,
    std::vector<TargetIdentifier> enum_cases,
    std::vector<std::optional<TargetPayloadEnumNames>> payload_enums,
    std::vector<TargetIdentifier> test_functions,
    std::vector<TargetIdentifier> module_runners
) noexcept
    : source_identity(semantic_identity),
      target_modules(std::move(modules)),
      target_generated_namespace(std::move(generated_namespace)),
      target_domain_namespace(std::move(domain_namespace)),
      target_function_names(std::move(functions)),
      target_structure_names(std::move(structures)),
      target_enumeration_names(std::move(enumerations)),
      target_callable_names(std::move(callables)),
      target_closure_type_names(std::move(closure_types)),
      target_enum_case_names(std::move(enum_cases)),
      target_payload_enums(std::move(payload_enums)),
      target_test_functions(std::move(test_functions)),
      target_module_runners(std::move(module_runners)) {
    if (target_modules.size() != target_module_runners.size()
        || target_callable_names.size() != target_closure_type_names.size()
        || target_enumeration_names.size() != target_payload_enums.size()) {
        invariant_violation("target name plan is not total over semantic rows");
    }
}

auto TargetNamePlan::semantic_owner() const noexcept -> ProgramIdentity {
    return source_identity;
}

auto TargetNamePlan::module(ModuleID id) const noexcept -> const TargetModuleNames& {
    return semantic_row(
        target_modules,
        source_identity,
        id,
        "target name plan used unknown module"
    );
}

auto TargetNamePlan::generated_namespace() const noexcept -> const TargetName& {
    return target_generated_namespace;
}

auto TargetNamePlan::domain_namespace() const noexcept -> const TargetName& {
    return target_domain_namespace;
}

auto TargetNamePlan::entity_name(
    ModuleID active_module,
    const TargetEntityName& entity
) const noexcept -> TargetName {
    if (active_module.owner() != source_identity) {
        invariant_violation("target entity name used a foreign active module");
    }
    auto relative = std::vector<TargetIdentifier>(
        entity.relative_name.components().begin(),
        entity.relative_name.components().end()
    );
    if (active_module == entity.owner_module) {
        return TargetName::from_components(std::move(relative));
    }
    const auto& target_namespace =
        module(entity.owner_module).qualified_namespace_name.components();
    auto qualified =
        std::vector<TargetIdentifier>(target_namespace.begin(), target_namespace.end());
    qualified.insert(qualified.end(), relative.begin(), relative.end());
    return TargetName::globally_qualified(std::move(qualified));
}

auto TargetNamePlan::global_entity_name(const TargetEntityName& entity) const noexcept
    -> TargetName {
    const auto& target_namespace =
        module(entity.owner_module).qualified_namespace_name.components();
    auto qualified =
        std::vector<TargetIdentifier>(target_namespace.begin(), target_namespace.end());
    qualified.insert(
        qualified.end(),
        entity.relative_name.components().begin(),
        entity.relative_name.components().end()
    );
    return TargetName::globally_qualified(std::move(qualified));
}

auto TargetNamePlan::function_identifier(FunctionID id) const noexcept -> const TargetIdentifier& {
    return semantic_row(
               target_function_names,
               source_identity,
               id,
               "target name plan used unknown function"
    )
        .relative_name.components()
        .back();
}

auto TargetNamePlan::function_name(ModuleID active_module, FunctionID id) const noexcept
    -> TargetName {
    return entity_name(
        active_module,
        semantic_row(
            target_function_names,
            source_identity,
            id,
            "target name plan used unknown function"
        )
    );
}

auto TargetNamePlan::global_function_name(FunctionID id) const noexcept -> TargetName {
    return global_entity_name(semantic_row(
        target_function_names,
        source_identity,
        id,
        "target name plan used unknown function"
    ));
}

auto TargetNamePlan::callable_owner(CallableID id) const noexcept -> ModuleID {
    const auto& value = semantic_row(
        target_callable_names,
        source_identity,
        id,
        "target name plan used unknown callable"
    );
    if (!value.has_value()) {
        invariant_violation("target name plan used an unnamed body callable as a direct callee");
    }
    return value->owner_module;
}

auto TargetNamePlan::structure_identifier(StructID id) const noexcept -> const TargetIdentifier& {
    return semantic_row(
               target_structure_names,
               source_identity,
               id,
               "target name plan used unknown structure"
    )
        .relative_name.components()
        .back();
}

auto TargetNamePlan::structure_name(ModuleID active_module, StructID id) const noexcept
    -> TargetName {
    return entity_name(
        active_module,
        semantic_row(
            target_structure_names,
            source_identity,
            id,
            "target name plan used unknown structure"
        )
    );
}

auto TargetNamePlan::enumeration_identifier(EnumID id) const noexcept -> const TargetIdentifier& {
    return semantic_row(
               target_enumeration_names,
               source_identity,
               id,
               "target name plan used unknown enumeration"
    )
        .relative_name.components()
        .back();
}

auto TargetNamePlan::enumeration_name(ModuleID active_module, EnumID id) const noexcept
    -> TargetName {
    return entity_name(
        active_module,
        semantic_row(
            target_enumeration_names,
            source_identity,
            id,
            "target name plan used unknown enumeration"
        )
    );
}

auto TargetNamePlan::enum_case_identifier(EnumCaseID id) const noexcept -> const TargetIdentifier& {
    return semantic_row(
        target_enum_case_names,
        source_identity,
        id,
        "target name plan used unknown enum case"
    );
}

auto TargetNamePlan::callable_name(ModuleID active_module, CallableID id) const noexcept
    -> TargetName {
    const auto& value = semantic_row(
        target_callable_names,
        source_identity,
        id,
        "target name plan used unknown callable"
    );
    if (!value.has_value()) {
        invariant_violation("target name plan used an unnamed body callable as a direct callee");
    }
    return entity_name(active_module, *value);
}

auto TargetNamePlan::closure_type_name(ModuleID active_module, CallableID id) const noexcept
    -> TargetName {
    const auto& value = semantic_row(
        target_closure_type_names,
        source_identity,
        id,
        "target name plan used unknown closure callable"
    );
    if (!value.has_value()) {
        invariant_violation("target name plan used a non-closure callable as a closure type");
    }
    return entity_name(active_module, *value);
}

auto TargetNamePlan::closure_owner(CallableID id) const noexcept -> ModuleID {
    const auto& value = semantic_row(
        target_closure_type_names,
        source_identity,
        id,
        "target name plan used unknown closure callable"
    );
    if (!value.has_value()) {
        invariant_violation("target name plan used a non-closure callable as a closure type");
    }
    return value->owner_module;
}

auto TargetNamePlan::payload_enum(EnumID enumeration) const noexcept
    -> const TargetPayloadEnumNames& {
    const auto& value = semantic_row(
        target_payload_enums,
        source_identity,
        enumeration,
        "target name plan used unknown enumeration"
    );
    if (!value.has_value()) {
        invariant_violation("target name plan requested payload names for a numeric enum");
    }
    return *value;
}

auto TargetNamePlan::test_function(TestID test) const noexcept -> const TargetIdentifier& {
    return semantic_row(
        target_test_functions,
        source_identity,
        test,
        "target name plan used unknown test"
    );
}

auto TargetNamePlan::module_runner(ModuleID module_id) const noexcept -> const TargetIdentifier& {
    return semantic_row(
        target_module_runners,
        source_identity,
        module_id,
        "target name plan used unknown module runner"
    );
}

FailureABI::FailureABI(
    ProgramIdentity semantic_identity,
    std::vector<std::vector<TypeID>> failure_sets
) noexcept
    : source_identity(semantic_identity),
      target_failure_sets(std::move(failure_sets)) {
    for (const auto& members : target_failure_sets) {
        for (const auto member : members) {
            if (member.owner() != source_identity) {
                invariant_violation("failure ABI references a foreign semantic type");
            }
        }
    }
}

auto FailureABI::semantic_owner() const noexcept -> ProgramIdentity {
    return source_identity;
}

auto FailureABI::members(FailureSetID set) const noexcept -> std::span<const TypeID> {
    if (set.owner() != source_identity || set.index() >= target_failure_sets.size()) {
        invariant_violation("failure ABI used a foreign or unknown semantic failure set");
    }
    return target_failure_sets[set.index()];
}

auto artifact_logical_path(const TargetArtifactPlan& artifact) noexcept -> std::string_view {
    return std::visit(
        []<typename Artifact>(const Artifact& value) static noexcept -> std::string_view {
            static_assert(
                std::same_as<Artifact, TargetInterfaceArtifact>
                    || std::same_as<Artifact, TargetCppAPIHeaderArtifact>
                    || std::same_as<Artifact, TargetModuleImplementationArtifact>
                    || std::same_as<Artifact, TargetTestRunnerHeaderArtifact>
                    || std::same_as<Artifact, TargetTestEntryArtifact>,
                "unhandled target artifact plan"
            );
            return value.logical_path;
        },
        artifact
    );
}

auto artifact_role(const TargetArtifactPlan& artifact) noexcept -> GeneratedArtifactRole {
    return std::visit(
        Overloaded {
            [](const TargetInterfaceArtifact&) static noexcept {
                return GeneratedArtifactRole::Interface;
            },
            [](const TargetCppAPIHeaderArtifact&) static noexcept {
                return GeneratedArtifactRole::CppAPIHeader;
            },
            [](const TargetModuleImplementationArtifact&) static noexcept {
                return GeneratedArtifactRole::ModuleImplementation;
            },
            [](const TargetTestRunnerHeaderArtifact&) static noexcept {
                return GeneratedArtifactRole::TestRunnerHeader;
            },
            [](const TargetTestEntryArtifact&) static noexcept {
                return GeneratedArtifactRole::TestEntry;
            },
        },
        artifact
    );
}

auto artifact_source_mapping(const TargetArtifactPlan& artifact) noexcept
    -> ArtifactSourceMappingPolicy {
    return std::visit(
        Overloaded {
            [](const TargetInterfaceArtifact&) static noexcept {
                return ArtifactSourceMappingPolicy::StableInterface;
            },
            [](const TargetCppAPIHeaderArtifact&) static noexcept {
                return ArtifactSourceMappingPolicy::StableInterface;
            },
            [](const TargetTestRunnerHeaderArtifact&) static noexcept {
                return ArtifactSourceMappingPolicy::StableInterface;
            },
            [](const TargetModuleImplementationArtifact&) static noexcept {
                return ArtifactSourceMappingPolicy::SourceAttributed;
            },
            [](const TargetTestEntryArtifact&) static noexcept {
                return ArtifactSourceMappingPolicy::SourceAttributed;
            },
        },
        artifact
    );
}

auto artifact_dependencies(const TargetArtifactPlan& artifact) noexcept
    -> std::vector<TargetArtifactID> {
    return std::visit(
        Overloaded {
            [](const TargetInterfaceArtifact& value) static noexcept {
                return value.predecessor_artifacts;
            },
            [](const TargetModuleImplementationArtifact& value) static noexcept {
                return value.interface_dependencies;
            },
            [](const TargetTestEntryArtifact& value) static noexcept {
                return std::vector {value.runner_header_dependency};
            },
            [](const TargetCppAPIHeaderArtifact& value) noexcept {
                return value.interface_dependencies;
            },
            [](const TargetTestRunnerHeaderArtifact&) static noexcept {
                return std::vector<TargetArtifactID>();
            },
        },
        artifact
    );
}

TargetPlan::TargetPlan(
    ProgramIdentity semantic_identity,
    TargetPlanIdentity identity,
    TargetNamePlan names,
    FailureABI failure_abi,
    TargetPlanTable<TargetArtifactPlan, TargetArtifactID> artifacts
) noexcept
    : source_identity(semantic_identity),
      plan_identity(identity),
      name_plan(std::move(names)),
      failure_abi_plan(std::move(failure_abi)),
      artifact_plans(std::move(artifacts)),
      active(true) {
    if (name_plan.semantic_owner() != source_identity
        || failure_abi_plan.semantic_owner() != source_identity) {
        invariant_violation("target plan combines metadata from different semantic programs");
    }
    if (artifact_plans.owner() != plan_identity) {
        invariant_violation("target plan combines metadata from different target plans");
    }
    if (artifact_plans.empty()) {
        invariant_violation("target plan must contain at least one artifact");
    }
    auto artifact_logical_paths = std::vector<std::string>();
    artifact_logical_paths.reserve(artifact_plans.size());
    for (const auto entry : artifact_plans.entries()) {
        artifact_logical_paths.emplace_back(artifact_logical_path(entry.value));
        for (const auto dependency : artifact_dependencies(entry.value)) {
            if (dependency.owner() != plan_identity || dependency.index() >= entry.id.index()) {
                invariant_violation("target artifact schedule is not dependency-first");
            }
        }
    }
    verify_target_artifact_logical_paths(artifact_logical_paths);
}

TargetPlan::TargetPlan(TargetPlan&& other) noexcept
    : source_identity(other.source_identity),
      plan_identity(other.plan_identity),
      name_plan(std::move(other.name_plan)),
      failure_abi_plan(std::move(other.failure_abi_plan)),
      artifact_plans(std::move(other.artifact_plans)),
      active(std::exchange(other.active, false)) {}

auto TargetPlan::require_active() const noexcept -> void {
    if (!active) {
        invariant_violation("target plan was used after move");
    }
}

auto TargetPlan::build(const SemIRProgram& semantic, const TargetPlanningRequest& request) noexcept
    -> TargetPlan {
    const auto identity = TargetPlanIdentity::fresh();
    const auto linkage = derive_linkage_domain_id(request);
    const auto closures = plan_closures(semantic);
    auto names = plan_names(semantic, linkage, closures);
    auto failures = plan_failure_abi(semantic);
    auto artifacts = plan_artifacts(semantic, request, closures, identity);
    return TargetPlan(
        semantic.identity(),
        identity,
        std::move(names),
        std::move(failures),
        std::move(artifacts)
    );
}

auto TargetPlan::semantic_identity() const noexcept -> ProgramIdentity {
    require_active();
    return source_identity;
}
auto TargetPlan::identity() const noexcept -> TargetPlanIdentity {
    require_active();
    return plan_identity;
}
auto TargetPlan::names() const noexcept -> const TargetNamePlan& {
    require_active();
    return name_plan;
}
auto TargetPlan::failure_abi() const noexcept -> const FailureABI& {
    require_active();
    return failure_abi_plan;
}
auto TargetPlan::artifacts() const noexcept
    -> TargetPlanTableEntries<TargetArtifactPlan, TargetArtifactID> {
    require_active();
    return artifact_plans.entries();
}
auto TargetPlan::artifact_count() const noexcept -> std::size_t {
    require_active();
    return artifact_plans.size();
}
auto TargetPlan::artifact(TargetArtifactID id) const noexcept -> const TargetArtifactPlan& {
    require_active();
    return artifact_plans.get(id);
}

PlannedCompilation::PlannedCompilation(SemIRProgram semantic, TargetPlan target) noexcept
    : semantic_program(std::move(semantic)),
      target_plan(std::move(target)) {
    if (semantic_program.identity() != target_plan.semantic_identity()) {
        invariant_violation("planned compilation paired a target plan with a different SemIR");
    }
}

auto PlannedCompilation::build(SemIRProgram semantic, const TargetPlanningRequest& request) noexcept
    -> PlannedCompilation {
    auto target = TargetPlan::build(semantic, request);
    return PlannedCompilation(std::move(semantic), std::move(target));
}

auto PlannedCompilation::require_active() const noexcept -> void {
    static_cast<void>(semantic_program.identity());
}

auto PlannedCompilation::semantic() const noexcept -> const SemIRProgram& {
    require_active();
    return semantic_program;
}

auto PlannedCompilation::target() const noexcept -> const TargetPlan& {
    require_active();
    return target_plan;
}

auto module_implementation_logical_path(std::span<const std::string> canonical_components) noexcept
    -> std::string {
    return logical_path({}, canonical_components, ".cpp");
}

auto interface_component_logical_path(std::span<const std::string> anchor_components) noexcept
    -> std::string {
    return logical_path("carven/generated", anchor_components, ".hpp");
}

auto cpp_api_header_logical_path(std::span<const std::string> canonical_components) noexcept
    -> std::string {
    return logical_path("carven/api", canonical_components, ".hpp");
}
