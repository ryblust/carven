module carven:backend.generation.program;

import :artifacts;
import :backend.generation.names;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :compilation.request;
import :semantic.hir;
import :semantic.hir.access;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.pattern;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :source.provenance;
import :support.typed_id;
import std;

struct TargetArtifactIDTag final {};
struct TargetCallSignatureIDTag final {};
struct TargetCarrierShapeIDTag final {};

using TargetArtifactID = TypedID<TargetArtifactIDTag>;
using TargetCallSignatureID = TypedID<TargetCallSignatureIDTag>;
using TargetCarrierShapeID = TypedID<TargetCarrierShapeIDTag>;

struct TargetArtifactIncludeDirective final {
    TargetArtifactID artifact;
};

using TargetArtifactDirective = std::variant<TargetDirective, TargetArtifactIncludeDirective>;

struct TargetArtifactDirectiveGroup final {
    std::vector<TargetArtifactDirective> directives;
};

struct TargetEntityName final {
    ProgramModuleID owner_module;
    TargetName relative_name;
};

struct TargetFailureProfile final {
    std::vector<HIRTypeID> ordered_members;
};

enum class TargetParameterPassing {
    Value,
    ConstReference,
    MutableReference,
};

struct TargetCallParameterRecipe final {
    HIRTypeID type;
    TargetParameterPassing passing;
};

struct TargetCallSignatureRecipe final {
    std::vector<TargetCallParameterRecipe> parameters;
    HIRTypeID result;
    FailureSetID failure_profile;
    std::optional<TargetCarrierShapeID> carrier_shape;
};

struct TargetIntrinsicTypeRecipe final {
    TargetSymbol symbol;
};

struct TargetNamedTypeRecipe final {
    SymbolID symbol;
};

struct TargetArrayTypeRecipe final {
    HIRTypeID element;
    TargetArrayExtent extent;
};

struct TargetFunctionReferenceTypeRecipe final {
    TargetCallSignatureID signature;
};

struct TargetCallableTypeRecipe final {
    TargetCallSignatureID signature;
};

struct TargetDeducedTypeRecipe final {};

using TargetTypeRecipeValue = std::variant<
    TargetIntrinsicTypeRecipe,
    TargetNamedTypeRecipe,
    TargetArrayTypeRecipe,
    TargetFunctionReferenceTypeRecipe,
    TargetCallableTypeRecipe,
    TargetDeducedTypeRecipe>;

struct TargetTypeRecipe final {
    TargetTypeRecipeValue value;
    bool read_parameter_by_value;
    bool integer;
    bool void_type;
    bool foreign;
};

struct TargetCarrierShape final {
    HIRTypeID result;
    FailureSetID failure_profile;
};

enum class TargetCarrierConversion {
    Identity,
    Widen,
};

struct TargetModuleNames final {
    TargetName qualified_namespace_name;
    TargetName module_namespace_name;
    std::flat_set<std::string> reserved_identifiers;
};

struct TargetInterfaceForwardDeclaration final {
    ProgramModuleID module_id;
    HIRNominalDeclRef declaration;
};

struct TargetInterfaceDeclaration final {
    ProgramModuleID module_id;
    HIRDeclarationRef declaration;
};

struct TargetInterfaceSchedule final {
    std::vector<ProgramModuleID> component_members;
    std::vector<TargetInterfaceForwardDeclaration> forward_declarations;
    std::vector<TargetInterfaceDeclaration> declarations;
};

struct TargetModuleSchedule final {
    ProgramModuleID module_id;
    std::vector<HIRNominalDeclRef> implementation_nominal_order;
    std::vector<std::uint32_t> cpp_preamble_items;
    std::vector<FunctionID> private_function_declarations;
    std::vector<FunctionID> function_definitions;
    std::optional<FunctionID> entry_point;
    std::vector<TestID> emitted_tests;
};

struct TargetTestEntrySchedule final {};

using TargetArtifactSchedule =
    std::variant<TargetInterfaceSchedule, TargetModuleSchedule, TargetTestEntrySchedule>;

struct TargetArtifactSpec final {
    std::string logical_path;
    GeneratedArtifactRole role;
    ArtifactSourceMappingPolicy source_mapping;
    std::vector<TargetArtifactDirectiveGroup> directive_groups;
    TargetArtifactSchedule schedule;
};

class TargetProgramBuilder;
class TargetArtifactView;

class TargetProgram final {
public:
    static auto build(SemanticProgram semantic, TargetGenerationRequest request) noexcept
        -> TargetProgram;

    TargetProgram(const TargetProgram&) = delete;
    TargetProgram(TargetProgram&&) = default;
    ~TargetProgram() = default;

    auto operator=(const TargetProgram&) -> TargetProgram& = delete;
    auto operator=(TargetProgram&&) -> TargetProgram& = default;

    auto artifacts() const noexcept -> std::span<const TargetArtifactSpec>;
    auto artifact(TargetArtifactID id) const noexcept -> const TargetArtifactSpec&;
    auto focused_artifact(TargetArtifactID id) const noexcept -> TargetArtifactView;

private:
    friend class TargetArtifactView;
    friend class TargetProgramBuilder;

    TargetProgram(
        SemanticProgram semantic,
        std::vector<TargetModuleNames> modules,
        std::vector<TargetArtifactSpec> artifacts,
        std::vector<TargetTypeRecipe> types,
        std::vector<TargetFailureProfile> failure_profiles,
        std::vector<TargetCallSignatureRecipe> call_signatures,
        std::vector<TargetCallSignatureID> callable_signatures,
        std::vector<TargetCarrierShape> carrier_shapes,
        std::flat_map<std::pair<std::uint32_t, std::uint32_t>, TargetCarrierShapeID> carrier_index,
        std::vector<std::uint8_t> carrier_conversions,
        std::vector<std::uint8_t> mutable_value_binding_flags,
        TargetName generated_namespace,
        TargetName domain_namespace,
        std::vector<std::optional<TargetEntityName>> entity_names,
        std::vector<std::optional<TargetPayloadEnumNames>> payload_enums,
        std::vector<std::flat_set<std::string>> scoped_source_names
    ) noexcept;

    auto resolve_name(ProgramModuleID active_module, const TargetEntityName& name) const noexcept
        -> TargetName;

    SemanticProgram semantic_program;
    std::vector<TargetModuleNames> target_modules;
    std::vector<TargetArtifactSpec> target_artifacts;
    std::vector<TargetTypeRecipe> target_types;
    std::vector<TargetFailureProfile> target_failure_profiles;
    std::vector<TargetCallSignatureRecipe> target_call_signatures;
    std::vector<TargetCallSignatureID> target_callable_signatures;
    std::vector<TargetCarrierShape> target_carrier_shapes;
    std::flat_map<std::pair<std::uint32_t, std::uint32_t>, TargetCarrierShapeID>
        target_carrier_index;
    std::vector<std::uint8_t> target_carrier_conversions;
    std::vector<std::uint8_t> mutable_value_binding_flags;
    TargetName target_generated_namespace;
    TargetName target_domain_namespace;
    std::vector<std::optional<TargetEntityName>> target_entity_names;
    std::vector<std::optional<TargetPayloadEnumNames>> target_payload_enums;
    std::vector<std::flat_set<std::string>> target_scoped_source_names;
};

class TargetArtifactView final {
public:
    auto artifact() const noexcept -> const TargetArtifactSpec&;
    auto materialize_directive_groups() const noexcept -> std::vector<TargetDirectiveGroup>;
    auto module(ProgramModuleID module_id) const noexcept -> const TargetModuleNames&;
    auto module_count() const noexcept -> std::size_t;
    auto type_count() const noexcept -> std::size_t;
    auto generated_namespace() const noexcept -> const TargetName&;
    auto domain_namespace() const noexcept -> const TargetName&;
    auto entity_identifier(SymbolID symbol) const noexcept -> const TargetIdentifier&;
    auto entity_name(ProgramModuleID active_module, SymbolID symbol) const noexcept -> TargetName;
    auto payload_enum(EnumID enumeration) const noexcept -> const TargetPayloadEnumNames&;
    auto source_names(SemanticScopeID scope) const noexcept -> const std::flat_set<std::string>&;
    auto type_recipe(HIRTypeID id) const noexcept -> const TargetTypeRecipe&;
    auto failure_profile(FailureSetID id) const noexcept -> const TargetFailureProfile&;
    auto callable_signature(CallableID id) const noexcept -> const TargetCallSignatureRecipe&;
    auto call_signature(TargetCallSignatureID id) const noexcept
        -> const TargetCallSignatureRecipe&;
    auto carrier_shape(HIRTypeID result, FailureSetID failure_profile) const noexcept
        -> TargetCarrierShapeID;
    auto carrier_shape(TargetCarrierShapeID id) const noexcept -> const TargetCarrierShape&;
    auto classify_failure_profile_conversion(
        FailureSetID source,
        FailureSetID destination
    ) const noexcept -> TargetCarrierConversion;
    auto classify_carrier_conversion(
        TargetCarrierShapeID source,
        TargetCarrierShapeID destination
    ) const noexcept -> TargetCarrierConversion;
    auto requires_mutable_value_binding(SymbolID symbol) const noexcept -> bool;

    auto provenance() const noexcept -> CompilationProvenanceView;
    auto type(HIRTypeID id) const noexcept -> const HIRType&;
    auto expression(HIRExprID id) const noexcept -> const HIRExpr&;
    auto expression_control(HIRExprID id) const noexcept -> const HIRExpressionControl&;
    auto evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect&;
    auto try_facts(HIRExprID id) const noexcept -> const std::optional<HIRTryFacts>&;
    auto constant(HIRConstantID id) const noexcept -> const HIRConstantFact&;
    auto statement(HIRStmtID id) const noexcept -> const HIRStmt&;
    auto pattern(HIRPatternID id) const noexcept -> const HIRPattern&;
    auto block(HIRBlockID id) const noexcept -> const HIRBlock&;
    auto block_control(HIRBlockID id) const noexcept -> const HIRBlockControl&;
    auto binding(SymbolID id) const noexcept -> const std::optional<SemanticBindingFacts>&;
    auto function(FunctionID id) const noexcept -> const HIRFunctionDecl&;
    auto body(BodyID id) const noexcept -> const HIRBody&;
    auto test(TestID id) const noexcept -> const HIRTestDecl&;
    auto structure(StructID id) const noexcept -> const HIRStructDecl&;
    auto enumeration(EnumID id) const noexcept -> const HIREnumDecl&;
    auto enum_case(EnumCaseID id) const noexcept -> const HIREnumCase&;
    auto nominal_capabilities(HIRNominalDeclRef id) const noexcept -> const HIRNominalCapabilities&;
    auto symbol(SymbolID id) const noexcept -> const HIRSymbol&;
    auto hir_module(ProgramModuleID id) const noexcept -> const HIRModule&;
    auto callable(CallableID id) const noexcept -> const HIRCallable&;

private:
    TargetArtifactView(const TargetProgram& program, TargetArtifactID artifact_id) noexcept;

    const TargetProgram* target_program;
    TargetArtifactID focused_artifact_id;

    friend class TargetProgram;
};
