module carven:semantic.analysis.session.storage.impl;

import :semantic.analysis.failures;
import :semantic.analysis.session;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.stmt;
import :support.invariant;
import :support.visit;
import std;

auto SemanticDraft::intern_string(std::string_view value) noexcept -> ProgramSpellingID {
    return provenance_builder.intern_spelling(value);
}

auto SemanticDraft::append_origin(ProgramOrigin value) noexcept -> ProgramOriginID {
    return provenance_builder.append_origin(std::move(value));
}

auto SemanticDraft::intern_type(HIRType value) noexcept -> HIRTypeID {
    if (const auto found = storage.type_index.find(value.value);
        found != storage.type_index.end()) {
        return found->second;
    }
    const auto id = storage.types.add(std::move(value));
    storage.type_index.emplace(storage.types.get(id).value, id);
    return id;
}

auto SemanticDraft::intern_failure_set(std::vector<HIRTypeID> failures) noexcept -> FailureSetID {
    failures = normalize_failure_members(std::move(failures));
    const auto value = HIRFailureSet {.members = std::move(failures)};
    if (const auto found = storage.failure_set_index.find(value);
        found != storage.failure_set_index.end()) {
        return found->second;
    }
    const auto id = storage.failure_sets.add(value);
    storage.failure_set_index.emplace(storage.failure_sets.get(id), id);
    return id;
}

auto SemanticDraft::intern_callable_signature(HIRCallableSignature value) noexcept
    -> CallableSignatureID {
    if (const auto found = storage.callable_signature_index.find(value);
        found != storage.callable_signature_index.end()) {
        return found->second;
    }
    const auto id = storage.callable_signatures.add(std::move(value));
    storage.callable_signature_index.emplace(storage.callable_signatures.get(id), id);
    return id;
}

auto SemanticDraft::intern_callable_signature(
    std::vector<HIRFunctionParameterType> parameters,
    HIRTypeID result,
    std::vector<HIRTypeID> failures
) noexcept -> CallableSignatureID {
    return intern_callable_signature({
        .parameters = std::move(parameters),
        .result = result,
        .failure_set = intern_failure_set(std::move(failures)),
    });
}

auto SemanticDraft::append_callable(
    std::vector<HIRFunctionParameterType> parameters,
    HIRTypeID result,
    std::vector<HIRTypeID> failures,
    SemanticFailureContractKind failure_contract
) noexcept -> CallableID {
    if (body_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic bodies exhausted their 32-bit identity space");
    }
    const auto body_id = BodyID::from_index(static_cast<std::uint32_t>(body_slots.size()));
    body_slots.emplace_back();
    const auto declared_failure_set = intern_failure_set(std::move(failures));
    const auto callable = storage.callables.add({
        .parameters = std::move(parameters),
        .result = result,
        .body = body_id,
    });
    if (callable.index() != callable_failure_input_storage.size()) {
        invariant_violation("callable failure inputs are not aligned with callables");
    }
    callable_failure_input_storage.push_back({
        .declared_failure_set = declared_failure_set,
        .policy = failure_contract,
    });
    return callable;
}

auto SemanticDraft::intern_function_ref_type(
    std::vector<HIRFunctionParameterType> parameters,
    HIRTypeID result,
    std::vector<HIRTypeID> failures
) noexcept -> HIRTypeID {
    return intern_type({
        .value = HIRFunctionRefTypeValue {
            .signature =
                intern_callable_signature(std::move(parameters), result, std::move(failures)),
        },
    });
}

auto SemanticDraft::intern_function_type(CallableID callable) noexcept -> HIRTypeID {
    return intern_type({.value = HIRFunctionTypeValue {.callable = callable}});
}

auto SemanticDraft::intern_closure_type(CallableID callable, bool capturing) noexcept -> HIRTypeID {
    return intern_type({
        .value = HIRClosureTypeValue {
            .callable = callable,
            .capturing = capturing,
        },
    });
}

auto SemanticDraft::append_expression(HIRExpr value) noexcept -> HIRExprID {
    const auto id = storage.expressions.add(std::move(value));
    expression_place_uses.emplace_back();
    return id;
}

auto SemanticDraft::append_constant(HIRConstantFact value) noexcept -> HIRConstantID {
    return storage.constants.add(std::move(value));
}

auto SemanticDraft::begin_expression_proof() const noexcept -> AnalysisExpressionProofCheckpoint {
    return {.expression_count = storage.expressions.size()};
}

auto SemanticDraft::finish_expression_proof(AnalysisExpressionProofCheckpoint checkpoint) noexcept
    -> void {
    if (checkpoint.expression_count > storage.expressions.size()) {
        invariant_violation("semantic expression proof belongs to another builder");
    }
    storage.expressions.rewind(checkpoint.expression_count);
    expression_place_uses.resize(checkpoint.expression_count);
}

auto SemanticDraft::append_statement(HIRStmt value) noexcept -> HIRStmtID {
    return storage.statements.add(std::move(value));
}

auto SemanticDraft::append_pattern(HIRPattern value) noexcept -> HIRPatternID {
    return storage.patterns.add(std::move(value));
}

auto SemanticDraft::append_block(HIRBlock value) noexcept -> HIRBlockID {
    return storage.blocks.add(std::move(value));
}

auto SemanticDraft::append_scope(std::optional<SemanticScopeID> parent) noexcept
    -> SemanticScopeID {
    if (parent.has_value() && !storage.scopes.contains(*parent)) {
        invariant_violation("semantic scope parent is not owned by this builder");
    }
    return storage.scopes.add({.parent = parent});
}

auto SemanticDraft::entity_reservations() noexcept -> SemanticEntityReservations {
    return SemanticEntityReservations(*this);
}

auto SemanticDraft::declaration_capabilities() noexcept -> SemanticDeclarationCapabilities {
    return SemanticDeclarationCapabilities(*this);
}

auto SemanticDraft::normalize_void_block(HIRBlockID id) noexcept -> HIRBlockID {
    auto& block = storage.blocks.get(id);
    if (!block.result.has_value()) {
        return id;
    }
    const auto result = *block.result;
    block.statements.push_back(append_statement({
        .origin = storage.expressions.get(result).origin,
        .value = HIRExprStmt {.expression = result},
    }));
    block.result.reset();
    return id;
}

auto SemanticDraft::append_module(HIRModule value) noexcept -> ProgramModuleID {
    return storage.modules.add(std::move(value));
}

auto SemanticDraft::expression(HIRExprID id) noexcept -> HIRExpr& {
    return storage.expressions.get(id);
}

auto SemanticDraft::statement(HIRStmtID id) noexcept -> HIRStmt& {
    return storage.statements.get(id);
}

auto SemanticDraft::block(HIRBlockID id) noexcept -> HIRBlock& {
    return storage.blocks.get(id);
}

auto SemanticDraft::body(BodyID id) noexcept -> HIRBody& {
    if (id.index() >= body_slots.size() || !body_slots[id.index()].has_value()) {
        invariant_violation("semantic body lookup used an undefined identity");
    }
    return *body_slots[id.index()];
}

auto SemanticDraft::hir_module(ProgramModuleID id) noexcept -> HIRModule& {
    return storage.modules.get(id);
}

auto SemanticDraft::publish_nominal_containment(HIRNominalContainment value) noexcept -> void {
    if (!storage.nominal_containment.direct_dependencies.empty()) {
        invariant_violation("nominal containment was published more than once");
    }
    if (value.direct_dependencies.size()
        != storage.structures.size() + storage.enumerations.size()) {
        invariant_violation("nominal containment dependencies are incomplete");
    }
    storage.nominal_containment = std::move(value);
}

auto SemanticDraft::type(HIRTypeID id) const noexcept -> const HIRType& {
    return storage.types.get(id);
}

auto SemanticDraft::expression(HIRExprID id) const noexcept -> const HIRExpr& {
    return storage.expressions.get(id);
}

auto SemanticDraft::expression_control(HIRExprID id) const noexcept -> const HIRExpressionControl& {
    return storage.expression_controls.get(id);
}

auto SemanticDraft::evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect& {
    return storage.evaluation_effects.get(id);
}

auto SemanticDraft::place_use(HIRExprID id) const noexcept
    -> const std::optional<SemanticPlaceUse>& {
    return storage.place_uses.get(id);
}

auto SemanticDraft::try_facts(HIRExprID id) const noexcept -> const std::optional<HIRTryFacts>& {
    return storage.try_facts.get(id);
}

auto SemanticDraft::constant(HIRConstantID id) const noexcept -> const HIRConstantFact& {
    return storage.constants.get(id);
}

auto SemanticDraft::statement(HIRStmtID id) const noexcept -> const HIRStmt& {
    return storage.statements.get(id);
}

auto SemanticDraft::pattern(HIRPatternID id) const noexcept -> const HIRPattern& {
    return storage.patterns.get(id);
}

auto SemanticDraft::block(HIRBlockID id) const noexcept -> const HIRBlock& {
    return storage.blocks.get(id);
}

auto SemanticDraft::block_control(HIRBlockID id) const noexcept -> const HIRBlockControl& {
    return storage.block_controls.get(id);
}

auto SemanticDraft::scope(SemanticScopeID id) const noexcept -> const SemanticScope& {
    return storage.scopes.get(id);
}

auto SemanticDraft::binding(SymbolID id) const noexcept
    -> const std::optional<SemanticBindingFacts>& {
    return storage.bindings.get(id);
}

auto SemanticDraft::function(FunctionID id) const noexcept -> const HIRFunctionDecl& {
    if (storage.functions.contains(id)) {
        return storage.functions.get(id);
    }
    if (id.index() >= function_slots.size() || !function_slots[id.index()].has_value()) {
        invariant_violation("function contract lookup used an unresolved reserved identity");
    }
    return *function_slots[id.index()];
}

auto SemanticDraft::has_body(BodyID id) const noexcept -> bool {
    return id.index() < body_slots.size() && body_slots[id.index()].has_value();
}

auto SemanticDraft::body(BodyID id) const noexcept -> const HIRBody& {
    if (id.index() >= body_slots.size() || !body_slots[id.index()].has_value()) {
        invariant_violation("semantic body lookup used an undefined identity");
    }
    return *body_slots[id.index()];
}

auto SemanticDraft::test(TestID id) const noexcept -> const HIRTestDecl& {
    return storage.tests.get(id);
}

auto SemanticDraft::structure(StructID id) const noexcept -> const HIRStructDecl& {
    if (storage.structures.contains(id)) {
        return storage.structures.get(id);
    }
    if (id.index() >= struct_slots.size() || !struct_slots[id.index()].has_value()) {
        invariant_violation("struct contract lookup used an unresolved reserved identity");
    }
    return *struct_slots[id.index()];
}

auto SemanticDraft::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    if (storage.enumerations.contains(id)) {
        return storage.enumerations.get(id);
    }
    if (id.index() >= enum_slots.size() || !enum_slots[id.index()].has_value()) {
        invariant_violation("enum contract lookup used an unresolved reserved identity");
    }
    return *enum_slots[id.index()];
}

auto SemanticDraft::enum_case(EnumCaseID id) const noexcept -> const HIREnumCase& {
    return storage.enum_cases.get(id);
}

auto SemanticDraft::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return symbol_state(id).symbol;
}

auto SemanticDraft::hir_module(ProgramModuleID id) const noexcept -> const HIRModule& {
    return storage.modules.get(id);
}

auto SemanticDraft::failure_set(FailureSetID id) const noexcept -> const HIRFailureSet& {
    return storage.failure_sets.get(id);
}

auto SemanticDraft::callable_signature(CallableSignatureID id) const noexcept
    -> const HIRCallableSignature& {
    return storage.callable_signatures.get(id);
}

auto SemanticDraft::callable(CallableID id) const noexcept -> const HIRCallable& {
    return storage.callables.get(id);
}

auto SemanticDraft::callable_flow(CallableID id) const noexcept -> const HIRCallableFlow& {
    return storage.callable_flows.get(id);
}

auto SemanticDraft::callable_failure_input(CallableID id) const noexcept
    -> const SemanticCallableFailureInput& {
    if (id.index() >= callable_failure_input_storage.size()) {
        invariant_violation("callable failure input lookup used an invalid identity or phase");
    }
    return callable_failure_input_storage[id.index()];
}

auto SemanticDraft::functions() const noexcept -> std::span<const HIRFunctionDecl> {
    return storage.functions.values();
}

auto SemanticDraft::bodies() const noexcept -> std::span<const std::optional<HIRBody>> {
    return body_slots;
}

auto SemanticDraft::tests() const noexcept -> std::span<const HIRTestDecl> {
    return storage.tests.values();
}

auto SemanticDraft::structures() const noexcept -> std::span<const HIRStructDecl> {
    return storage.structures.values();
}

auto SemanticDraft::enumerations() const noexcept -> std::span<const HIREnumDecl> {
    return storage.enumerations.values();
}

auto SemanticDraft::enum_cases() const noexcept -> std::span<const HIREnumCase> {
    return storage.enum_cases.values();
}

auto SemanticDraft::modules() const noexcept -> std::span<const HIRModule> {
    return storage.modules.values();
}

auto SemanticDraft::expressions() const noexcept -> std::span<const HIRExpr> {
    return storage.expressions.values();
}

auto SemanticDraft::expression_controls() const noexcept -> std::span<const HIRExpressionControl> {
    return storage.expression_controls.values();
}

auto SemanticDraft::evaluation_effects() const noexcept -> std::span<const EvaluationEffect> {
    return storage.evaluation_effects.values();
}

auto SemanticDraft::place_uses() const noexcept
    -> std::span<const std::optional<SemanticPlaceUse>> {
    return storage.place_uses.values();
}

auto SemanticDraft::try_facts() const noexcept -> std::span<const std::optional<HIRTryFacts>> {
    return storage.try_facts.values();
}

auto SemanticDraft::constants() const noexcept -> std::span<const HIRConstantFact> {
    return storage.constants.values();
}

auto SemanticDraft::types() const noexcept -> std::span<const HIRType> {
    return storage.types.values();
}

auto SemanticDraft::statements() const noexcept -> std::span<const HIRStmt> {
    return storage.statements.values();
}

auto SemanticDraft::patterns() const noexcept -> std::span<const HIRPattern> {
    return storage.patterns.values();
}

auto SemanticDraft::blocks() const noexcept -> std::span<const HIRBlock> {
    return storage.blocks.values();
}

auto SemanticDraft::block_controls() const noexcept -> std::span<const HIRBlockControl> {
    return storage.block_controls.values();
}

auto SemanticDraft::scopes() const noexcept -> std::span<const SemanticScope> {
    return storage.scopes.values();
}

auto SemanticDraft::bindings() const noexcept
    -> std::span<const std::optional<SemanticBindingFacts>> {
    return storage.bindings.values();
}

auto SemanticDraft::symbol_count() const noexcept -> std::size_t {
    return symbol_construction.size();
}

auto SemanticDraft::failure_sets() const noexcept -> std::span<const HIRFailureSet> {
    return storage.failure_sets.values();
}

auto SemanticDraft::callable_signatures() const noexcept -> std::span<const HIRCallableSignature> {
    return storage.callable_signatures.values();
}

auto SemanticDraft::callables() const noexcept -> std::span<const HIRCallable> {
    return storage.callables.values();
}

auto SemanticDraft::callable_flows() const noexcept -> std::span<const HIRCallableFlow> {
    return storage.callable_flows.values();
}

auto SemanticDraft::callable_failure_inputs() const noexcept
    -> std::span<const SemanticCallableFailureInput> {
    return callable_failure_input_storage;
}

auto SemanticDraft::nominal_containment(HIRNominalDeclRef declaration) const noexcept
    -> std::span<const HIRNominalDeclRef> {
    const auto index = std::visit(
        Overloaded {
            [](StructID id) static noexcept -> std::size_t { return id.index(); },
            [&](EnumID id) noexcept -> std::size_t {
                return storage.structures.size() + id.index();
            },
        },
        declaration
    );
    if (index >= storage.nominal_containment.direct_dependencies.size()) {
        std::unreachable();
    }
    return storage.nominal_containment.direct_dependencies[index];
}

auto SemanticDraft::nominal_capabilities(HIRNominalDeclRef declaration) const noexcept
    -> const HIRNominalCapabilities& {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept -> const HIRNominalCapabilities& {
                return storage.struct_capabilities.get(id);
            },
            [&](EnumID id) noexcept -> const HIRNominalCapabilities& {
                return storage.enum_capabilities.get(id);
            },
        },
        declaration
    );
}

auto SemanticDraft::structure_capabilities() const noexcept
    -> std::span<const HIRNominalCapabilities> {
    return storage.struct_capabilities.values();
}

auto SemanticDraft::enumeration_capabilities() const noexcept
    -> std::span<const HIRNominalCapabilities> {
    return storage.enum_capabilities.values();
}

auto SemanticDraft::nominal_dependency_sets() const noexcept
    -> std::span<const std::vector<HIRNominalDeclRef>> {
    return storage.nominal_containment.direct_dependencies;
}

auto SemanticDraft::provenance() const noexcept -> CompilationProvenanceView {
    return provenance_builder.view();
}
