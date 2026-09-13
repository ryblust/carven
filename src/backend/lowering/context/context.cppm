module carven:backend.lowering.context;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target;
import :semantic.semir;
import :support.unique_indirect;
import std;

class ModuleLowering;

enum class CppEnvironmentRequirement { Declarations, Using };

class ArtifactLowering final {
public:
    ArtifactLowering(const PlannedCompilation& compilation, TargetArtifactID artifact) noexcept;
    ArtifactLowering(const ArtifactLowering&) = delete;
    ArtifactLowering(ArtifactLowering&&) = default;
    ~ArtifactLowering() = default;

    auto operator=(const ArtifactLowering&) -> ArtifactLowering& = delete;
    auto operator=(ArtifactLowering&&) -> ArtifactLowering& = delete;
    auto semantic() const noexcept -> const SemIRProgram&;
    auto plan() const noexcept -> const TargetPlan&;
    auto artifact() const noexcept -> const TargetArtifactPlan&;
    auto target() noexcept -> TargetUnitBuilder&;
    auto module_context(ModuleID id) noexcept -> ModuleLowering;
    auto require_cpp_environment(ModuleID provider, CppEnvironmentRequirement requirement) noexcept
        -> void;
    auto finish(TargetUnitSections sections) && noexcept -> TargetUnit;

private:
    auto record_provider_interface(ModuleID active, ModuleID provider) noexcept -> void;

    const PlannedCompilation& planned_compilation;
    TargetArtifactID artifact_id;
    TargetUnitBuilder target_builder;
    std::flat_set<TargetArtifactID> lowering_dependencies;
    std::flat_map<ModuleID, CppEnvironmentRequirement> cpp_environments;

    auto materialize_cpp_environments(
        TargetUnitSections& sections,
        TargetDirectiveInputs& directives
    ) noexcept -> void;

    friend class ModuleLowering;
};

class ModuleLowering final {
public:
    ModuleLowering(ArtifactLowering& artifact, ModuleID owner_module_id) noexcept;
    ModuleLowering(const ModuleLowering&) = delete;
    ModuleLowering(ModuleLowering&&) = default;
    ~ModuleLowering() = default;

    auto operator=(const ModuleLowering&) -> ModuleLowering& = delete;
    auto operator=(ModuleLowering&&) -> ModuleLowering& = delete;
    auto semantic() const noexcept -> const SemIRProgram&;
    auto plan() const noexcept -> const TargetPlan&;
    auto target() noexcept -> TargetUnitBuilder&;
    auto active_module() const noexcept -> ModuleID;
    auto names() const noexcept -> const TargetNamePlan&;
    auto global_function_name(FunctionID id) noexcept -> TargetName;
    auto structure_name(StructID id) noexcept -> TargetName;
    auto enumeration_name(EnumID id) noexcept -> TargetName;
    auto callable_name(CallableID id) noexcept -> TargetName;
    auto closure_type_name(CallableID id) noexcept -> TargetName;
    auto payload_enum(EnumID id) noexcept -> const TargetPayloadEnumNames&;
    auto name_allocator() noexcept -> TargetNameAllocator&;
    auto make_callable_name_allocator() const noexcept -> TargetNameAllocator;
    auto intrinsic_type(TargetSymbol symbol, bool constant = false) noexcept -> TargetTypeID;
    auto named_type(TargetName name, bool constant = false) noexcept -> TargetTypeID;
    auto reference_type(TargetTypeID referent, bool constant = false, bool rvalue = false) noexcept
        -> TargetTypeID;
    auto pointer_type(TargetTypeID pointee, bool constant = false) noexcept -> TargetTypeID;
    auto optional_type(TargetTypeID value) noexcept -> TargetTypeID;
    auto variant_type(std::span<const TypeID> members) noexcept -> TargetTypeID;
    auto callable_result(CallableID callable_id) noexcept -> TargetTypeID;
    auto call_result(TypeID type) noexcept -> TargetTypeID;
    auto lower_type(TypeID id) noexcept -> TargetTypeID;
    auto cpp_name(const CppNameReference& name) noexcept -> TargetName;
    auto cpp_type_query(const CppQueryType& query) noexcept -> TargetExpr;
    auto lower_parameter(const CallableParameter& parameter) noexcept -> TargetTypeID;
    auto lower_signature_result(CallableSignatureID signature, bool stops_test) noexcept
        -> TargetTypeID;
    auto is_void(TypeID id) const noexcept -> bool;
    auto is_integer(TypeID id) const noexcept -> bool;

private:
    struct Resolving final {};

    using TypeState = std::variant<Resolving, TargetTypeID>;

    auto function_type(CallableSignatureID signature, bool stops_test) noexcept -> TargetType;

    ArtifactLowering& artifact_lowering;
    ModuleID module_id;
    TargetNameAllocator allocator;
    std::map<TypeID, TypeState> type_cache;
    std::map<std::pair<CallableSignatureID, bool>, TypeState> signature_result_cache;
};

auto target_child(TargetExpr expression) noexcept -> UniqueIndirect<TargetExpr>;
auto name_expression(TargetName name) noexcept -> TargetExpr;
auto name_expression(TargetIdentifier name) noexcept -> TargetExpr;
auto intrinsic_expression(TargetSymbol symbol) noexcept -> TargetExpr;
auto call_expression(TargetExpr callee, std::vector<TargetExpr> arguments) noexcept -> TargetExpr;
auto target_expressions(TargetExpr value) noexcept -> std::vector<TargetExpr>;
auto target_expressions(TargetExpr first, TargetExpr second) noexcept -> std::vector<TargetExpr>;
auto target_expressions(TargetExpr first, TargetExpr second, TargetExpr third) noexcept
    -> std::vector<TargetExpr>;
auto member_expression(TargetExpr operand, TargetMemberName member) noexcept -> TargetExpr;
auto scope_member_expression(TargetExpr operand, TargetMemberName member) noexcept -> TargetExpr;
auto static_member_expression(TargetTypeID owner, TargetIdentifier member) noexcept -> TargetExpr;
auto transfer_expression(TargetExpr value) noexcept -> TargetExpr;
auto address_expression(TargetExpr operand) noexcept -> TargetExpr;
auto dereference_expression(TargetExpr operand) noexcept -> TargetExpr;
auto integer_expression(std::uint64_t value) noexcept -> TargetExpr;
auto string_expression(std::string value, TargetStringLiteralKind kind) noexcept -> TargetExpr;
auto generated_statement(TargetStmtValue value) noexcept -> TargetStmt;

auto source_statement(
    const SemIRProgram& semantic,
    ProgramOriginID origin,
    TargetStmtValue value
) noexcept -> TargetStmt;

auto compiler_item(TargetItemValue value, TargetCompilerReason reason) noexcept -> TargetItem;

auto source_item(
    const SemIRProgram& semantic,
    ProgramOriginID origin,
    TargetItemValue value
) noexcept -> TargetItem;

auto expansion_item(
    const SemIRProgram& semantic,
    ProgramOriginID origin,
    TargetItemValue value
) noexcept -> TargetItem;

auto target_items(TargetItem item) noexcept -> std::vector<TargetItem>;

auto namespace_item(
    std::optional<TargetName> name,
    std::vector<TargetItem> items,
    TargetCompilerReason reason = TargetCompilerReason::ArtifactScaffolding,
    bool closing_comment = true
) noexcept -> TargetItem;
