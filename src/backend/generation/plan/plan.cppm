module carven:backend.generation.plan;

import :artifacts;
import :backend.generation.linkage;
import :backend.generation.names;
import :backend.generation.request;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :semantic.semir;
import std;

struct TargetEntityName final {
    ModuleID owner_module;
    TargetName relative_name;
};

struct TargetModuleNames final {
    TargetName qualified_namespace_name;
    TargetName module_namespace_name;
    TargetName public_namespace_name;
    std::flat_map<FunctionID, TargetIdentifier> public_functions;
    std::flat_set<std::string> reserved_identifiers;
};

struct TargetClosureCatalog final {
    ProgramIdentity semantic_identity;
    std::vector<std::optional<ModuleID>> owner_modules;
    std::vector<std::vector<CallableID>> production_definitions;
    std::vector<std::vector<CallableID>> test_definitions;
    std::vector<CallableID> definition_order;

    auto owner(CallableID callable) const noexcept -> ModuleID;
    auto production(ModuleID module_id) const noexcept -> std::span<const CallableID>;
    auto tests(ModuleID module_id) const noexcept -> std::span<const CallableID>;
};

class TargetNamePlan final {
public:
    TargetNamePlan(
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
    ) noexcept;

    TargetNamePlan(const TargetNamePlan&) = delete;
    TargetNamePlan(TargetNamePlan&&) = default;
    ~TargetNamePlan() = default;
    auto operator=(const TargetNamePlan&) -> TargetNamePlan& = delete;
    auto operator=(TargetNamePlan&&) -> TargetNamePlan& = delete;

    auto semantic_owner() const noexcept -> ProgramIdentity;
    auto module_names(ModuleID id) const noexcept -> const TargetModuleNames&;
    auto generated_namespace() const noexcept -> const TargetName&;
    auto domain_namespace() const noexcept -> const TargetName&;
    auto function_identifier(FunctionID id) const noexcept -> const TargetIdentifier&;
    auto function_name(ModuleID active_module, FunctionID id) const noexcept -> TargetName;
    auto global_function_name(FunctionID id) const noexcept -> TargetName;
    auto callable_owner(CallableID id) const noexcept -> ModuleID;
    auto structure_identifier(StructID id) const noexcept -> const TargetIdentifier&;
    auto structure_name(ModuleID active_module, StructID id) const noexcept -> TargetName;
    auto enumeration_identifier(EnumID id) const noexcept -> const TargetIdentifier&;
    auto enumeration_name(ModuleID active_module, EnumID id) const noexcept -> TargetName;
    auto enum_case_identifier(EnumCaseID id) const noexcept -> const TargetIdentifier&;
    auto callable_name(ModuleID active_module, CallableID id) const noexcept -> TargetName;
    auto closure_type_name(ModuleID active_module, CallableID id) const noexcept -> TargetName;
    auto closure_owner(CallableID id) const noexcept -> ModuleID;
    auto payload_enum(EnumID enumeration) const noexcept -> const TargetPayloadEnumNames&;
    auto test_function(TestID test) const noexcept -> const TargetIdentifier&;
    auto module_runner(ModuleID module_id) const noexcept -> const TargetIdentifier&;

private:
    auto entity_name(ModuleID active_module, const TargetEntityName& entity) const noexcept
        -> TargetName;
    auto global_entity_name(const TargetEntityName& entity) const noexcept -> TargetName;

    ProgramIdentity source_identity;
    std::vector<TargetModuleNames> target_modules;
    TargetName target_generated_namespace;
    TargetName target_domain_namespace;
    std::vector<TargetEntityName> target_function_names;
    std::vector<TargetEntityName> target_structure_names;
    std::vector<TargetEntityName> target_enumeration_names;
    std::vector<std::optional<TargetEntityName>> target_callable_names;
    std::vector<std::optional<TargetEntityName>> target_closure_type_names;
    std::vector<TargetIdentifier> target_enum_case_names;
    std::vector<std::optional<TargetPayloadEnumNames>> target_payload_enums;
    std::vector<TargetIdentifier> target_test_functions;
    std::vector<TargetIdentifier> target_module_runners;
};

class FailureABI final {
public:
    FailureABI(
        ProgramIdentity semantic_identity,
        std::vector<std::vector<TypeID>> failure_sets
    ) noexcept;

    FailureABI(const FailureABI&) = delete;
    FailureABI(FailureABI&&) = default;
    ~FailureABI() = default;
    auto operator=(const FailureABI&) -> FailureABI& = delete;
    auto operator=(FailureABI&&) -> FailureABI& = delete;

    auto semantic_owner() const noexcept -> ProgramIdentity;
    auto members(FailureSetID set) const noexcept -> std::span<const TypeID>;


private:
    ProgramIdentity source_identity;
    std::vector<std::vector<TypeID>> target_failure_sets;
};

struct TargetInterfaceForwardDeclaration final {
    ModuleID module_id;
    NominalDeclarationRef declaration;
};

struct TargetInterfaceDeclaration final {
    ModuleID module_id;
    std::variant<FunctionID, StructID, EnumID, CallableID> declaration;
};

struct TargetModuleSchedule final {
    ModuleID module_id;
    std::vector<NominalDeclarationRef> private_nominal_order;
    std::vector<CallableID> closure_definitions;
    std::vector<CallableID> interface_closures;
    std::vector<TestID> emitted_tests;
};

struct TargetInterfaceArtifact final {
    std::string logical_path;
    std::vector<ModuleID> component_members;
    std::vector<TargetArtifactID> predecessor_artifacts;
    std::vector<TargetInterfaceForwardDeclaration> forward_declarations;
    std::vector<TargetInterfaceDeclaration> declarations;
};

struct TargetCppAPIHeaderArtifact final {
    std::string logical_path;
    ModuleID module_id;
    std::vector<FunctionID> cpp_export_declarations;
    std::vector<TargetArtifactID> interface_dependencies;
};

struct TargetModuleImplementationArtifact final {
    std::string logical_path;
    TargetModuleSchedule schedule;
    std::vector<TargetArtifactID> interface_dependencies;
};

struct TargetTestRunnerHeaderArtifact final {
    std::string logical_path;
    std::vector<ModuleID> module_runners;
};

struct TargetTestEntryArtifact final {
    std::string logical_path;
    TargetArtifactID runner_header_dependency;
};

using TargetArtifactPlan = std::variant<
    TargetInterfaceArtifact,
    TargetCppAPIHeaderArtifact,
    TargetModuleImplementationArtifact,
    TargetTestRunnerHeaderArtifact,
    TargetTestEntryArtifact>;

auto artifact_logical_path(const TargetArtifactPlan& artifact) noexcept -> std::string_view;
auto artifact_role(const TargetArtifactPlan& artifact) noexcept -> GeneratedArtifactRole;
auto artifact_source_mapping(const TargetArtifactPlan& artifact) noexcept
    -> ArtifactSourceMappingPolicy;
auto artifact_dependencies(const TargetArtifactPlan& artifact) noexcept
    -> std::vector<TargetArtifactID>;
auto verify_target_artifact_logical_paths(std::span<const std::string> logical_paths) noexcept
    -> void;

class PlannedCompilation;

class TargetPlan final {
public:
    TargetPlan(const TargetPlan&) = delete;
    TargetPlan(TargetPlan&&) noexcept = default;
    ~TargetPlan() = default;
    auto operator=(const TargetPlan&) -> TargetPlan& = delete;
    auto operator=(TargetPlan&&) -> TargetPlan& = delete;

    auto semantic_identity() const noexcept -> ProgramIdentity;
    auto identity() const noexcept -> TargetPlanIdentity;
    auto names() const noexcept -> const TargetNamePlan&;
    auto failure_abi() const noexcept -> const FailureABI&;
    auto artifacts() const noexcept -> TargetPlanTableEntries<TargetArtifactPlan, TargetArtifactID>;
    auto artifact_count() const noexcept -> std::size_t;
    auto artifact(TargetArtifactID id) const noexcept -> const TargetArtifactPlan&;

private:
    static auto build(const SemIRProgram& semantic, const TargetPlanningRequest& request) noexcept
        -> TargetPlan;

    TargetPlan(
        ProgramIdentity semantic_identity,
        TargetPlanIdentity identity,
        TargetNamePlan names,
        FailureABI failure_abi,
        TargetPlanTable<TargetArtifactPlan, TargetArtifactID> artifacts
    ) noexcept;

    ProgramIdentity source_identity;
    TargetPlanIdentity plan_identity;
    TargetNamePlan name_plan;
    FailureABI failure_abi_plan;
    TargetPlanTable<TargetArtifactPlan, TargetArtifactID> artifact_plans;

    friend class PlannedCompilation;
};

class PlannedCompilation final {
public:
    static auto build(SemIRProgram semantic, const TargetPlanningRequest& request) noexcept
        -> PlannedCompilation;

    PlannedCompilation(const PlannedCompilation&) = delete;
    PlannedCompilation(PlannedCompilation&& other) noexcept = default;
    ~PlannedCompilation() = default;
    auto operator=(const PlannedCompilation&) -> PlannedCompilation& = delete;
    auto operator=(PlannedCompilation&&) -> PlannedCompilation& = delete;

    auto semantic() const noexcept -> const SemIRProgram&;
    auto target() const noexcept -> const TargetPlan&;

private:
    PlannedCompilation(SemIRProgram semantic, TargetPlan target) noexcept;

    SemIRProgram semantic_program;
    TargetPlan target_plan;
};

auto plan_closures(const SemIRProgram& semantic) noexcept -> TargetClosureCatalog;
auto plan_names(
    const SemIRProgram& semantic,
    const LinkageDomainID& linkage,
    const TargetClosureCatalog& closures
) noexcept -> TargetNamePlan;
auto plan_failure_abi(const SemIRProgram& semantic) noexcept -> FailureABI;

auto target_source_origin(CompilationProvenanceView provenance, ProgramOriginID origin) noexcept
    -> TargetSourceOrigin;
auto plan_artifacts(
    const SemIRProgram& semantic,
    const TargetPlanningRequest& request,
    const TargetClosureCatalog& closures,
    TargetPlanIdentity identity
) noexcept -> TargetPlanTable<TargetArtifactPlan, TargetArtifactID>;
auto module_implementation_logical_path(std::span<const std::string> canonical_components) noexcept
    -> std::string;
auto interface_component_logical_path(std::span<const std::string> anchor_components) noexcept
    -> std::string;
auto cpp_api_header_logical_path(std::span<const std::string> canonical_components) noexcept
    -> std::string;
auto materialize_directives(
    const TargetPlan& plan,
    TargetArtifactID artifact,
    std::span<const TargetArtifactID> lowering_dependencies = {}
) noexcept -> TargetDirectiveInputs;
