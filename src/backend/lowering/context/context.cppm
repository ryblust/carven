module carven:backend.lowering.context;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.constant.storage;
import :backend.target;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :semantic.semir.delegation;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.type;
import :support.unique_indirect;
import std;

class ModuleLowering;


enum class CppEnvironmentRequirement { Declarations, Using };

class ArtifactLowering final {
public:
    ArtifactLowering(const PlannedCompilation& compilation, TargetArtifactID artifact) noexcept;
    ArtifactLowering(const ArtifactLowering&) = delete;
    ArtifactLowering(ArtifactLowering&&) = delete;
    ~ArtifactLowering() = default;

    auto operator=(const ArtifactLowering&) -> ArtifactLowering& = delete;
    auto operator=(ArtifactLowering&&) -> ArtifactLowering& = delete;
    auto semantic() const noexcept -> const SemIRProgram&;
    auto plan() const noexcept -> const TargetPlan&;
    auto artifact() const noexcept -> const TargetArtifactPlan&;
    auto target() noexcept -> TargetUnitBuilder&;
    // Contexts live until finish(); map growth does not invalidate borrows.
    auto module_context(ModuleID id) noexcept -> ModuleLowering&;
    auto next_definition() noexcept -> std::optional<CallableID>;
    auto take_module_support() noexcept -> std::vector<TargetItem>;
    auto require_cpp_environment(ModuleID provider, CppEnvironmentRequirement requirement) noexcept
        -> void;
    // The definitions placed at their uses that the definition of `id` named.
    auto use_placed_callees(CallableID id) const noexcept -> const std::set<CallableID>&;
    auto finish(TargetUnitSections sections) && noexcept -> TargetUnit;

private:
    auto require_interface(ModuleID provider) noexcept -> void;
    auto require_callable(ModuleID owner, CallableID id) noexcept -> void;

    const PlannedCompilation& planned_compilation;
    TargetArtifactID artifact_id;
    TargetUnitBuilder target_builder;
    std::flat_set<TargetArtifactID> lowering_dependencies;
    std::flat_map<ModuleID, CppEnvironmentRequirement> cpp_environments;
    std::set<CallableID> required_definitions;
    std::deque<CallableID> pending_definitions;
    // The definition handed out last; names it requires are its callees.
    std::optional<CallableID> active_definition;
    std::map<CallableID, std::set<CallableID>> use_placed_edges;
    std::map<ModuleID, std::unique_ptr<ModuleLowering>> modules;


    auto materialize_cpp_environments(
        TargetUnitSections& sections,
        TargetDirectiveInputs& directives
    ) noexcept -> void;

    friend class ModuleLowering;
};

enum class TypeNameScope { Module, Global };

class ModuleLowering final {
public:
    ModuleLowering(ArtifactLowering& artifact, ModuleID owner_module_id) noexcept;
    ModuleLowering(const ModuleLowering&) = delete;
    ModuleLowering(ModuleLowering&&) = delete;
    ~ModuleLowering() = default;

    auto operator=(const ModuleLowering&) -> ModuleLowering& = delete;
    auto operator=(ModuleLowering&&) -> ModuleLowering& = delete;
    auto semantic() const noexcept -> const SemIRProgram&;
    auto plan() const noexcept -> const TargetPlan&;
    auto target() noexcept -> TargetUnitBuilder&;
    auto active_module() const noexcept -> ModuleID;
    auto names() const noexcept -> const TargetNamePlan&;
    auto global_function_name(FunctionID id) noexcept -> TargetName;
    auto structure_name(StructID id, TypeNameScope scope = TypeNameScope::Module) noexcept
        -> TargetName;
    auto field_identifier(StructID owner, std::size_t index) const noexcept -> TargetIdentifier;
    auto enumeration_name(EnumID id, TypeNameScope scope = TypeNameScope::Module) noexcept
        -> TargetName;
    auto callable_name(CallableID id) noexcept -> TargetName;
    auto closure_type_name(CallableID id, TypeNameScope scope = TypeNameScope::Module) noexcept
        -> TargetName;
    auto require_callable(CallableID id) noexcept -> void;
    auto payload_enum(EnumID id) noexcept -> const TargetPayloadEnumNames&;
    auto constant_storage() noexcept -> ConstantStorage&;
    auto make_callable_name_allocator() const noexcept -> TargetNameAllocator;
    auto intrinsic_type(TargetSymbol symbol, bool constant = false) noexcept -> TargetTypeID;
    auto named_type(TargetName name, bool constant = false) noexcept -> TargetTypeID;
    auto reference_type(TargetTypeID referent, bool constant = false, bool rvalue = false) noexcept
        -> TargetTypeID;
    auto pointer_type(TargetTypeID pointee, bool constant = false) noexcept -> TargetTypeID;
    auto optional_type(TargetTypeID value) noexcept -> TargetTypeID;
    auto variant_type(std::span<const TypeID> members) noexcept -> TargetTypeID;
    auto callable_result(CallableID callable_id) noexcept -> TargetTypeID;
    auto call_result(const SemCall& call) noexcept -> TargetTypeID;
    auto lower_type(TypeID id, TypeNameScope scope = TypeNameScope::Module) noexcept
        -> TargetTypeID;
    auto cpp_name(const CppNameReference& name) noexcept -> TargetName;
    auto cpp_constant_argument(
        const CppConstructArgument& argument,
        TypeNameScope scope = TypeNameScope::Module
    ) noexcept -> TargetExpr;
    auto lower_cpp_query(TypeID query_type) noexcept -> TargetTypeID;
    auto lower_parameter(
        AccessMode access,
        TypeID type,
        TypeNameScope scope = TypeNameScope::Module
    ) noexcept -> TargetTypeID;
    auto lower_signature_result(
        CallableSignatureID signature,
        bool stops_test,
        TypeNameScope scope = TypeNameScope::Module
    ) noexcept -> TargetTypeID;
    auto display_emitter_type(TypeID type) noexcept -> TargetTypeID;
    auto display_emitter(TypeID type) noexcept -> TargetExpr;
    auto take_query_aliases() noexcept -> std::vector<TargetItem>;
    auto take_display_helpers() noexcept -> std::vector<TargetItem>;
    auto is_void(TypeID id) const noexcept -> bool;
    auto is_integer(TypeID id) const noexcept -> bool;

private:
    struct Resolving final {};

    using TypeState = std::variant<Resolving, TargetTypeID>;

    auto function_type(CallableSignatureID signature, bool stops_test, TypeNameScope scope) noexcept
        -> TargetType;
    auto cpp_type_query(const CppQueryType& query, TypeNameScope scope) noexcept -> TargetExpr;

    ArtifactLowering& artifact_lowering;
    ModuleID module_id;
    ConstantStorage constants;
    std::map<TypeID, TargetTypeID> query_types;
    std::vector<TargetItem> query_aliases;
    std::map<std::pair<TypeID, TypeNameScope>, TypeState> type_cache;
    std::map<TypeID, TargetTypeID> display_types;
    std::vector<TargetItem> display_helpers;
    std::vector<TargetItem> display_definitions;
    std::map<std::tuple<CallableSignatureID, bool, TypeNameScope>, TypeState>
        signature_result_cache;
};

auto target_child(TargetExpr expression) noexcept -> UniqueIndirect<TargetExpr>;
auto name_expression(TargetName name) noexcept -> TargetExpr;
auto name_expression(TargetIdentifier name) noexcept -> TargetExpr;
auto name_expression(TargetLocalID local) noexcept -> TargetExpr;
auto intrinsic_expression(TargetSymbol symbol) noexcept -> TargetExpr;
auto target_expressions(TargetExpr value) noexcept -> std::vector<TargetExpr>;
auto target_expressions(TargetExpr first, TargetExpr second) noexcept -> std::vector<TargetExpr>;
auto target_expressions(TargetExpr first, TargetExpr second, TargetExpr third) noexcept
    -> std::vector<TargetExpr>;
auto member_expression(TargetExpr operand, TargetMemberName member) noexcept -> TargetExpr;
auto scope_member_expression(TargetExpr operand, TargetMemberName member) noexcept -> TargetExpr;
auto static_member_expression(TargetTypeID owner, TargetIdentifier member) noexcept -> TargetExpr;
auto transfer_expression(TargetExpr value) noexcept -> TargetExpr;
auto native_take_expression(ModuleLowering& context, TypeID type, TargetExpr value) noexcept
    -> TargetExpr;
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
