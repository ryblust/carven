module carven:backend.lowering.program;

import :backend.target.builder;
import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.unit;
import :semantic.hir;
import :semantic.hir.ids;
import :compilation.request;
import std;

struct FailureCarrierDescriptor final {
    HIRTypeID result;
    FailureSetID failure_set;
    TargetTypeID type;
};

struct FailureContinuation final {
    FailureCarrierDescriptor carrier;
    std::optional<TargetExprID> destination;
    std::optional<TargetIdentifier> transfer_label;
};

struct CaughtFailureContext final {
    FailureCarrierDescriptor carrier;
    TargetExprID expression;
    std::optional<TargetTypeID> selected_failure_type;
};

struct TestContinuation final {
    std::optional<TargetTypeID> result;
};

struct ContinueDestination final {
    TargetIdentifier label;
    std::reference_wrapper<bool> used;
};

struct TargetControlDestinations final {
    static auto callable(std::optional<FailureContinuation> failure = std::nullopt) noexcept
        -> TargetControlDestinations;
    static auto test() noexcept -> TargetControlDestinations;

    auto iife() const noexcept -> TargetControlDestinations;
    auto with_failure(FailureContinuation failure) const noexcept -> TargetControlDestinations;
    auto with_caught_failure(CaughtFailureContext failure) const noexcept
        -> TargetControlDestinations;
    auto with_test_result(TargetTypeID result) const noexcept -> TargetControlDestinations;
    auto with_continue_destination(std::optional<ContinueDestination> destination) const noexcept
        -> TargetControlDestinations;

    std::optional<FailureContinuation> callable_failure;
    std::optional<FailureContinuation> failure;
    std::optional<CaughtFailureContext> caught_failure;
    std::optional<TestContinuation> test_exit;
    std::optional<ContinueDestination> continue_destination;
};

class TargetModuleLowerer;
class TargetCallableLowerer;
class TargetLexicalScope;
class TargetCallableLowerer;

class TargetGenerationContext final {
public:
    TargetGenerationContext(
        const SemanticProgram& semantic,
        const TargetGenerationPlan& plan,
        TestEmissionMode test_mode
    ) noexcept;

    auto semantic() const noexcept -> const SemanticProgram&;
    auto read_parameter_by_value(HIRTypeID type) const noexcept -> bool;
    auto failure_set(FailureSetID failure_set) const noexcept -> const TargetFailureSetProfile&;
    auto requires_mutable_value_binding(SemanticPlaceID place) const noexcept -> bool;
    auto payload_enum(EnumID enumeration) const noexcept -> const TargetPayloadEnumNames&;
    auto source_names(SemanticScopeID scope) const noexcept -> const std::flat_set<std::string>&;
    auto target() noexcept -> TargetUnitBuilder&;
    auto emits_tests() const noexcept -> bool;
    auto emits_default_test_runner() const noexcept -> bool;
    auto module_lowerer(ProgramModuleID module_id) noexcept -> TargetModuleLowerer;
    auto entity_identifier(SymbolID symbol) const noexcept -> const TargetIdentifier&;
    auto entity_name(ProgramModuleID active, SymbolID symbol) const noexcept -> TargetName;
    auto cached_type(ProgramModuleID module_id, HIRTypeID type) const noexcept
        -> std::optional<TargetTypeID>;
    auto cache_type(ProgramModuleID module_id, HIRTypeID type, TargetTypeID lowered) noexcept
        -> void;
    auto finish(TargetUnitRoot root) && noexcept -> TargetUnit;

private:
    const SemanticProgram& semantic_program;
    const TargetGenerationPlan& generation_plan;
    TestEmissionMode test_mode;
    TargetUnitBuilder target_unit;
    std::flat_map<std::pair<std::uint32_t, std::uint32_t>, TargetTypeID> lowered_types;

    friend class TargetModuleLowerer;
};

class TargetModuleLowerer {
public:
    TargetModuleLowerer(const TargetModuleLowerer&) = delete;
    TargetModuleLowerer(TargetModuleLowerer&&) = default;
    ~TargetModuleLowerer() = default;

    auto operator=(const TargetModuleLowerer&) -> TargetModuleLowerer& = delete;
    auto operator=(TargetModuleLowerer&&) -> TargetModuleLowerer& = default;

    auto semantic() const noexcept -> const SemanticProgram&;
    auto read_parameter_by_value(HIRTypeID type) const noexcept -> bool;
    auto failure_set(FailureSetID failure_set) const noexcept -> const TargetFailureSetProfile&;
    auto requires_mutable_value_binding(SemanticPlaceID place) const noexcept -> bool;
    auto payload_enum(EnumID enumeration) const noexcept -> const TargetPayloadEnumNames&;
    auto target() noexcept -> TargetUnitBuilder&;
    auto name_allocator() noexcept -> TargetNameAllocator&;
    auto name_scope(SemanticScopeID scope) const noexcept -> TargetScopeID;
    auto emits_tests() const noexcept -> bool;
    auto active_module_id() const noexcept -> ProgramModuleID;
    auto entity_identifier(SymbolID symbol) const noexcept -> const TargetIdentifier&;
    auto entity_name(SymbolID symbol) const noexcept -> TargetName;
    auto cached_type(HIRTypeID type) const noexcept -> std::optional<TargetTypeID>;
    auto cache_type(HIRTypeID type, TargetTypeID lowered) noexcept -> void;
    auto reserve_source_names(SemanticScopeID scope) noexcept -> void;
    auto callable(
        SemanticScopeID scope,
        std::optional<SemanticScopeID> root_body_scope = std::nullopt
    ) noexcept -> TargetCallableLowerer;

protected:
    TargetModuleLowerer(TargetGenerationContext& program, ProgramModuleID module_id) noexcept;

private:
    TargetGenerationContext* program_lowerer;
    ProgramModuleID source_module_id;
    TargetNameAllocator target_names;

    friend class TargetGenerationContext;
    friend class TargetCallableLowerer;
};

class TargetCallableLowerer final : public TargetModuleLowerer {
public:
    TargetCallableLowerer(const TargetCallableLowerer&) = delete;
    TargetCallableLowerer(TargetCallableLowerer&&) = default;
    ~TargetCallableLowerer() = default;

    auto operator=(const TargetCallableLowerer&) -> TargetCallableLowerer& = delete;
    auto operator=(TargetCallableLowerer&&) -> TargetCallableLowerer& = default;

    auto nested_callable(SemanticScopeID scope, SemanticScopeID root_body_scope) noexcept
        -> TargetCallableLowerer;
    auto enter_scope(SemanticScopeID scope) noexcept -> TargetLexicalScope;
    auto fresh_name(TargetTemporaryNameKind kind) noexcept -> TargetIdentifier;

private:
    TargetCallableLowerer(
        TargetGenerationContext& program,
        ProgramModuleID module_id,
        SemanticScopeID scope,
        std::optional<SemanticScopeID> root_body_scope
    ) noexcept;

    auto target_scope(SemanticScopeID scope) const noexcept -> SemanticScopeID;

    SemanticScopeID callable_scope;
    SemanticScopeID lexical_scope;
    std::optional<SemanticScopeID> source_root_body_scope;

    friend class TargetModuleLowerer;
    friend class TargetLexicalScope;
};

class TargetLexicalScope final {
public:
    TargetLexicalScope(const TargetLexicalScope&) = delete;
    TargetLexicalScope(TargetLexicalScope&& other) noexcept;
    ~TargetLexicalScope() noexcept;

    auto operator=(const TargetLexicalScope&) -> TargetLexicalScope& = delete;
    auto operator=(TargetLexicalScope&&) -> TargetLexicalScope& = delete;

private:
    TargetLexicalScope(TargetCallableLowerer& context, SemanticScopeID scope) noexcept;

    TargetCallableLowerer* callable;
    SemanticScopeID previous_scope;

    friend class TargetCallableLowerer;
};

auto target_source_origin(const TargetModuleLowerer& context, ProgramOriginID id) noexcept
    -> TargetSourceOrigin;
