module carven:backend.lowering.program.impl;

import :backend.lowering.program;
import :backend.generation.names;
import :backend.lowering.names;
import :backend.target.unit;
import :semantic.hir.decl;
import :support.invariant;
import std;

auto TargetControlDestinations::callable(std::optional<FailureContinuation> active_failure) noexcept
    -> TargetControlDestinations {
    auto result = TargetControlDestinations();
    result.callable_failure = active_failure;
    result.failure = std::move(active_failure);
    return result;
}

auto TargetControlDestinations::test() noexcept -> TargetControlDestinations {
    auto result = TargetControlDestinations();
    result.test_exit = TestContinuation {.result = std::nullopt};
    return result;
}

auto TargetControlDestinations::iife() const noexcept -> TargetControlDestinations {
    auto result = *this;
    if (result.failure.has_value()) {
        result.failure->destination = std::nullopt;
        result.failure->transfer_label = std::nullopt;
    }
    result.continue_destination.reset();
    return result;
}

auto TargetControlDestinations::with_failure(FailureContinuation value) const noexcept
    -> TargetControlDestinations {
    auto result = *this;
    result.failure = std::move(value);
    return result;
}

auto TargetControlDestinations::with_caught_failure(CaughtFailureContext value) const noexcept
    -> TargetControlDestinations {
    auto result = *this;
    result.caught_failure = std::move(value);
    return result;
}

auto TargetControlDestinations::with_test_result(TargetTypeID value) const noexcept
    -> TargetControlDestinations {
    auto result = *this;
    result.test_exit = TestContinuation {.result = value};
    return result;
}

auto TargetControlDestinations::with_continue_destination(
    std::optional<ContinueDestination> value
) const noexcept -> TargetControlDestinations {
    auto result = *this;
    result.continue_destination = std::move(value);
    return result;
}

TargetGenerationContext::TargetGenerationContext(
    const SemanticProgram& semantic,
    const TargetGenerationPlan& plan,
    TestEmissionMode tests
) noexcept
    : semantic_program(semantic),
      generation_plan(plan),
      test_mode(tests) {}

auto TargetGenerationContext::semantic() const noexcept -> const SemanticProgram& {
    return semantic_program;
}

auto TargetGenerationContext::read_parameter_by_value(HIRTypeID type) const noexcept -> bool {
    return generation_plan.read_parameter_by_value(type);
}

auto TargetGenerationContext::failure_set(FailureSetID failure_set) const noexcept
    -> const TargetFailureSetProfile& {
    return generation_plan.failure_set(failure_set);
}

auto TargetGenerationContext::requires_mutable_value_binding(SemanticPlaceID place) const noexcept
    -> bool {
    return generation_plan.requires_mutable_value_binding(place);
}

auto TargetGenerationContext::payload_enum(EnumID enumeration) const noexcept
    -> const TargetPayloadEnumNames& {
    return generation_plan.payload_enum(enumeration);
}

auto TargetGenerationContext::source_names(SemanticScopeID scope) const noexcept
    -> const std::flat_set<std::string>& {
    return generation_plan.source_names(scope);
}

auto TargetGenerationContext::target() noexcept -> TargetUnitBuilder& {
    return target_unit;
}

auto TargetGenerationContext::emits_tests() const noexcept -> bool {
    return test_mode != TestEmissionMode::None;
}

auto TargetGenerationContext::emits_default_test_runner() const noexcept -> bool {
    return test_mode == TestEmissionMode::DefaultRunner;
}

auto TargetGenerationContext::module_lowerer(ProgramModuleID module_id) noexcept
    -> TargetModuleLowerer {
    if (module_id.index() >= semantic_program.modules().size()) {
        invariant_violation("target lowering requested an unknown source module");
    }
    return TargetModuleLowerer(*this, module_id);
}

auto TargetGenerationContext::entity_identifier(SymbolID symbol) const noexcept
    -> const TargetIdentifier& {
    return generation_plan.entity_identifier(symbol);
}

auto TargetGenerationContext::entity_name(ProgramModuleID active, SymbolID symbol) const noexcept
    -> TargetName {
    return generation_plan.entity_name(active, symbol);
}

auto TargetGenerationContext::cached_type(ProgramModuleID module_id, HIRTypeID type) const noexcept
    -> std::optional<TargetTypeID> {
    if (module_id.index() >= semantic_program.modules().size()
        || type.index() >= semantic_program.types().size()) {
        invariant_violation("type cache lookup used an invalid HIR type identity");
    }
    const auto found = lowered_types.find({module_id.index(), type.index()});
    return found == lowered_types.end() ? std::nullopt : std::optional(found->second);
}

auto TargetGenerationContext::cache_type(
    ProgramModuleID module_id,
    HIRTypeID type,
    TargetTypeID lowered
) noexcept -> void {
    if (module_id.index() >= semantic_program.modules().size()
        || type.index() >= semantic_program.types().size()) {
        invariant_violation("type cache insertion used an invalid HIR type identity");
    }
    const auto [position, inserted] =
        lowered_types.emplace(std::pair {module_id.index(), type.index()}, lowered);
    if (!inserted && position->second != lowered) {
        invariant_violation("type cache key was lowered to inconsistent target types");
    }
}

auto TargetGenerationContext::finish(TargetUnitRoot root) && noexcept -> TargetUnit {
    return std::move(target_unit).finish(std::move(root));
}

TargetModuleLowerer::TargetModuleLowerer(
    TargetGenerationContext& program,
    ProgramModuleID module_id
) noexcept
    : program_lowerer(std::addressof(program)),
      source_module_id(module_id) {
    for (const auto& name : program.generation_plan.module_plan(module_id).reserved_identifiers) {
        target_names.reserve(name);
    }
}

auto TargetModuleLowerer::semantic() const noexcept -> const SemanticProgram& {
    return program_lowerer->semantic();
}

auto TargetModuleLowerer::read_parameter_by_value(HIRTypeID type) const noexcept -> bool {
    return program_lowerer->read_parameter_by_value(type);
}

auto TargetModuleLowerer::failure_set(FailureSetID failure_set) const noexcept
    -> const TargetFailureSetProfile& {
    return program_lowerer->failure_set(failure_set);
}

auto TargetModuleLowerer::requires_mutable_value_binding(SemanticPlaceID place) const noexcept
    -> bool {
    return program_lowerer->requires_mutable_value_binding(place);
}

auto TargetModuleLowerer::payload_enum(EnumID enumeration) const noexcept
    -> const TargetPayloadEnumNames& {
    return program_lowerer->payload_enum(enumeration);
}

auto TargetModuleLowerer::target() noexcept -> TargetUnitBuilder& {
    return program_lowerer->target();
}

auto TargetModuleLowerer::name_allocator() noexcept -> TargetNameAllocator& {
    return target_names;
}

auto TargetModuleLowerer::name_scope(SemanticScopeID scope) const noexcept -> TargetScopeID {
    return target_names.canonical_scope(TargetScopeID {.ordinal = scope.index()});
}

auto TargetModuleLowerer::emits_tests() const noexcept -> bool {
    return program_lowerer->emits_tests();
}

auto TargetModuleLowerer::active_module_id() const noexcept -> ProgramModuleID {
    return source_module_id;
}

auto TargetModuleLowerer::entity_identifier(SymbolID symbol) const noexcept
    -> const TargetIdentifier& {
    return program_lowerer->entity_identifier(symbol);
}

auto TargetModuleLowerer::entity_name(SymbolID symbol) const noexcept -> TargetName {
    return program_lowerer->entity_name(source_module_id, symbol);
}

auto TargetModuleLowerer::cached_type(HIRTypeID type) const noexcept
    -> std::optional<TargetTypeID> {
    return program_lowerer->cached_type(source_module_id, type);
}

auto TargetModuleLowerer::cache_type(HIRTypeID type, TargetTypeID lowered) noexcept -> void {
    program_lowerer->cache_type(source_module_id, type, lowered);
}

auto TargetModuleLowerer::callable(
    SemanticScopeID scope,
    std::optional<SemanticScopeID> root_body_scope
) noexcept -> TargetCallableLowerer {
    return TargetCallableLowerer(*program_lowerer, source_module_id, scope, root_body_scope);
}

TargetCallableLowerer::TargetCallableLowerer(
    TargetGenerationContext& program,
    ProgramModuleID module_id,
    SemanticScopeID scope,
    std::optional<SemanticScopeID> root_body_scope
) noexcept
    : TargetModuleLowerer(program, module_id),
      callable_scope(scope),
      lexical_scope(scope),
      source_root_body_scope(root_body_scope) {
    if (root_body_scope.has_value()) {
        name_allocator().alias_scope(
            TargetScopeID {.ordinal = root_body_scope->index()},
            TargetScopeID {.ordinal = scope.index()}
        );
    }
    reserve_source_names(scope);
    if (root_body_scope.has_value()) {
        reserve_source_names(*root_body_scope);
    }
}

auto TargetCallableLowerer::nested_callable(
    SemanticScopeID scope,
    SemanticScopeID root_body_scope
) noexcept -> TargetCallableLowerer {
    return callable(scope, root_body_scope);
}

auto TargetCallableLowerer::enter_scope(SemanticScopeID scope) noexcept -> TargetLexicalScope {
    reserve_source_names(scope);
    return TargetLexicalScope(*this, target_scope(scope));
}

auto TargetModuleLowerer::reserve_source_names(SemanticScopeID scope) noexcept -> void {
    const auto target_scope = name_scope(scope);
    for (const auto& name : program_lowerer->source_names(scope)) {
        target_names.reserve(name, target_scope);
    }
}

auto TargetCallableLowerer::fresh_name(TargetTemporaryNameKind kind) noexcept -> TargetIdentifier {
    return name_allocator().fresh(kind, TargetScopeID {.ordinal = lexical_scope.index()});
}

auto TargetCallableLowerer::target_scope(SemanticScopeID scope) const noexcept -> SemanticScopeID {
    return source_root_body_scope == scope ? callable_scope : scope;
}

TargetLexicalScope::TargetLexicalScope(
    TargetCallableLowerer& context,
    SemanticScopeID scope
) noexcept
    : callable(std::addressof(context)),
      previous_scope(context.lexical_scope) {
    context.lexical_scope = scope;
}

TargetLexicalScope::TargetLexicalScope(TargetLexicalScope&& other) noexcept
    : callable(std::exchange(other.callable, nullptr)),
      previous_scope(other.previous_scope) {}

TargetLexicalScope::~TargetLexicalScope() noexcept {
    if (callable != nullptr) {
        callable->lexical_scope = previous_scope;
    }
}

auto target_source_origin(const TargetModuleLowerer& context, ProgramOriginID id) noexcept
    -> TargetSourceOrigin {
    const auto& origin = context.semantic().provenance().origin(id);
    const auto& source = context.semantic().provenance().source_snapshot(origin.source_id);
    return {
        .display_origin = std::string(source.display_origin()),
        .line = context.semantic().provenance().location(id).line,
    };
}
