module carven:semantic.analysis.builder.impl;

import :semantic.analysis.builder;
import :semantic.analysis.failures;
import :semantic.analysis.validation.invariants;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :semantic.hir.type;
import :support.invariant;
import std;

SemanticConstruction::SemanticConstruction(CompilationProvenance provenance) noexcept
    : provenance_builder(std::move(provenance)) {}

auto SemanticConstruction::intern_string(std::string_view value) noexcept -> ProgramSpellingID {
    return provenance_builder.intern_spelling(value);
}

auto SemanticConstruction::append_origin(ProgramOrigin value) noexcept -> ProgramOriginID {
    return provenance_builder.append_origin(std::move(value));
}

auto SemanticConstruction::intern_type(HIRType value) noexcept -> HIRTypeID {
    if (const auto found = storage.type_index.find(value.value);
        found != storage.type_index.end()) {
        return found->second;
    }
    const auto id = storage.types.add(std::move(value));
    storage.type_index.emplace(storage.types.get(id).value, id);
    return id;
}

auto SemanticConstruction::intern_failure_set(std::vector<HIRTypeID> failures) noexcept
    -> FailureSetID {
    failures = normalize_failure_members(*this, std::move(failures));
    const auto value = HIRFailureSet {.members = std::move(failures)};
    if (const auto found = storage.failure_set_index.find(value);
        found != storage.failure_set_index.end()) {
        return found->second;
    }
    const auto id = storage.failure_sets.add(value);
    storage.failure_set_index.emplace(storage.failure_sets.get(id), id);
    return id;
}

auto SemanticConstruction::intern_callable_signature(HIRCallableSignature value) noexcept
    -> CallableSignatureID {
    if (const auto found = storage.callable_signature_index.find(value);
        found != storage.callable_signature_index.end()) {
        return found->second;
    }
    const auto id = storage.callable_signatures.add(std::move(value));
    storage.callable_signature_index.emplace(storage.callable_signatures.get(id), id);
    return id;
}

auto SemanticConstruction::intern_callable_signature(
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

auto SemanticConstruction::append_callable(
    std::vector<HIRFunctionParameterType> parameters,
    HIRTypeID result,
    std::vector<HIRTypeID> failures,
    HIRFailureContractKind failure_contract
) noexcept -> CallableID {
    if (body_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic bodies exhausted their 32-bit identity space");
    }
    const auto body_id = BodyID::from_index(static_cast<std::uint32_t>(body_slots.size()));
    body_slots.emplace_back();
    return storage.callables.add({
        .parameters = std::move(parameters),
        .result = result,
        .failure_set = intern_failure_set(std::move(failures)),
        .failure_contract = failure_contract,
        .body = body_id,
    });
}

auto SemanticConstruction::intern_function_ref_type(
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

auto SemanticConstruction::intern_function_type(CallableID callable) noexcept -> HIRTypeID {
    return intern_type({.value = HIRFunctionTypeValue {.callable = callable}});
}

auto SemanticConstruction::intern_closure_type(CallableID callable, bool capturing) noexcept
    -> HIRTypeID {
    return intern_type({
        .value = HIRClosureTypeValue {
            .callable = callable,
            .capturing = capturing,
        },
    });
}

auto SemanticConstruction::append_expression(HIRExpr value) noexcept -> HIRExprID {
    const auto id = storage.expressions.add(std::move(value));
    expression_place_uses.emplace_back();
    return id;
}

auto SemanticConstruction::append_constant(HIRConstantFact value) noexcept -> HIRConstantID {
    return storage.constants.add(std::move(value));
}

auto SemanticConstruction::begin_expression_proof() const noexcept
    -> AnalysisExpressionProofCheckpoint {
    return {.expression_count = storage.expressions.size()};
}

auto SemanticConstruction::finish_expression_proof(
    AnalysisExpressionProofCheckpoint checkpoint
) noexcept -> void {
    if (checkpoint.expression_count > storage.expressions.size()) {
        invariant_violation("semantic expression proof belongs to another builder");
    }
    storage.expressions.rewind(checkpoint.expression_count);
    expression_place_uses.resize(checkpoint.expression_count);
}

auto SemanticConstruction::append_statement(HIRStmt value) noexcept -> HIRStmtID {
    return storage.statements.add(std::move(value));
}

auto SemanticConstruction::append_pattern(HIRPattern value) noexcept -> HIRPatternID {
    return storage.patterns.add(std::move(value));
}

auto SemanticConstruction::append_block(HIRBlock value) noexcept -> HIRBlockID {
    return storage.blocks.add(std::move(value));
}

auto SemanticConstruction::append_scope(std::optional<SemanticScopeID> parent) noexcept
    -> SemanticScopeID {
    if (parent.has_value() && !storage.scopes.contains(*parent)) {
        invariant_violation("semantic scope parent is not owned by this builder");
    }
    return storage.scopes.add({.parent = parent});
}

auto SemanticConstruction::append_symbol(SemanticSymbolSpec spec) noexcept -> SymbolID {
    const auto symbol =
        SymbolID::from_index(static_cast<std::uint32_t>(symbol_construction.size()));
    symbol_construction.push_back({
        .symbol =
            {
                .name = spec.name,
                .module_id = spec.module_id,
                .type = std::nullopt,
                .parent = spec.parent,
                .place = std::nullopt,
                .referenced = false,
            },
        .unused_candidate = std::nullopt,
        .constant = std::nullopt,
        .binding_position = std::nullopt,
        .role = spec.role,
        .binding_role = SemanticBindingRole::None,
        .explicit_capture = false,
        .write_eligible = false,
    });
    return symbol;
}

auto SemanticConstruction::symbol_state(SymbolID symbol) noexcept -> SemanticSymbolState& {
    if (symbol.index() >= symbol_construction.size()) {
        invariant_violation("semantic symbol construction lookup used an invalid identity");
    }
    return symbol_construction[symbol.index()];
}

auto SemanticConstruction::symbol_state(SymbolID symbol) const noexcept
    -> const SemanticSymbolState& {
    if (symbol.index() >= symbol_construction.size()) {
        invariant_violation("semantic symbol construction lookup used an invalid identity");
    }
    return symbol_construction[symbol.index()];
}

auto SemanticConstruction::adopt_symbol_type(SymbolID symbol, HIRTypeID type) noexcept -> void {
    auto& state = symbol_state(symbol);
    if (state.symbol.type.has_value()) {
        invariant_violation("semantic symbol type was defined more than once");
    }
    state.symbol.type = type;
}

auto SemanticConstruction::define_symbol_constant(SymbolID symbol, HIRConstantID constant) noexcept
    -> void {
    auto& state = symbol_state(symbol);
    if (state.constant.has_value()) {
        invariant_violation("semantic symbol constant was defined more than once");
    }
    state.constant = constant;
}

auto SemanticConstruction::define_symbol_binding(
    SymbolID symbol,
    SemanticBindingRole role,
    bool write_eligible
) noexcept -> void {
    auto& state = symbol_state(symbol);
    if (state.binding_role != SemanticBindingRole::None) {
        invariant_violation("semantic symbol binding role was defined more than once");
    }
    state.binding_role = role;
    state.write_eligible = write_eligible;
}

auto SemanticConstruction::mark_symbol_referenced(SymbolID symbol) noexcept -> void {
    symbol_state(symbol).symbol.referenced = true;
}

auto SemanticConstruction::mark_explicit_capture(SymbolID symbol) noexcept -> void {
    symbol_state(symbol).explicit_capture = true;
}

auto SemanticConstruction::record_symbol_lint_candidate(
    SymbolID symbol,
    ProgramOriginID origin
) noexcept -> void {
    auto& state = symbol_state(symbol);
    if (state.unused_candidate.has_value()) {
        invariant_violation("semantic symbol lint candidate was recorded more than once");
    }
    state.unused_candidate = origin;
}

auto SemanticConstruction::symbol_constant(SymbolID symbol) const noexcept
    -> std::optional<HIRConstantID> {
    return symbol_state(symbol).constant;
}

auto SemanticConstruction::symbol_role(SymbolID symbol) const noexcept -> SemanticSymbolRole {
    return symbol_state(symbol).role;
}

auto SemanticConstruction::symbol_write_eligible(SymbolID symbol) const noexcept -> bool {
    return symbol_state(symbol).write_eligible;
}

auto SemanticConstruction::symbol_states() const noexcept -> std::span<const SemanticSymbolState> {
    return symbol_construction;
}

auto SemanticConstruction::bind_symbol(SymbolID symbol, SemanticBindingPosition position) noexcept
    -> void {
    if (symbol.index() >= symbol_construction.size() || !storage.scopes.contains(position.scope)) {
        invariant_violation("semantic binding references an unknown symbol or scope");
    }
    auto& slot = symbol_state(symbol).binding_position;
    if (slot.has_value()) {
        invariant_violation("semantic symbol was bound to more than one lexical scope");
    }
    slot = position;
}

auto SemanticConstruction::publish_place(
    SymbolID symbol,
    SemanticPlaceStorage place_storage,
    SemanticPlaceCapabilities capabilities
) noexcept -> SemanticPlaceID {
    auto& state = symbol_state(symbol);
    if (!state.binding_position.has_value()) {
        invariant_violation("runtime binding has no lexical position");
    }
    auto& semantic_symbol = state.symbol;
    if (!semantic_symbol.type.has_value() || semantic_symbol.place.has_value()) {
        invariant_violation("runtime binding has no type or already owns a place");
    }
    const auto position = *state.binding_position;
    const auto place = storage.places.add({
        .symbol = symbol,
        .type = *semantic_symbol.type,
        .storage = place_storage,
        .capabilities = capabilities,
        .scope = position.scope,
        .declaration_order = position.declaration_order,
    });
    semantic_symbol.place = place;
    return place;
}

auto SemanticConstruction::derive_places() noexcept -> void {
    for (auto index = 0uz; index < symbol_construction.size(); ++index) {
        const auto symbol = SymbolID::from_index(static_cast<std::uint32_t>(index));
        const auto& state = symbol_construction[index];
        switch (state.binding_role) {
            case SemanticBindingRole::None:
            case SemanticBindingRole::CompileTime: continue;
            case SemanticBindingRole::Owner:
                publish_place(
                    symbol,
                    SemanticPlaceStorage::Owner,
                    {.write = state.write_eligible, .take = true}
                );
                break;
            case SemanticBindingRole::ReadAlias:
                publish_place(
                    symbol,
                    SemanticPlaceStorage::Borrow,
                    {.write = false, .take = false}
                );
                break;
            case SemanticBindingRole::WriteAlias:
                publish_place(symbol, SemanticPlaceStorage::Borrow, {.write = true, .take = false});
                break;
            case SemanticBindingRole::ClosureState:
                publish_place(
                    symbol,
                    SemanticPlaceStorage::Compiler,
                    {.write = false, .take = false}
                );
                break;
        }
    }
    derive_place_uses();
}

auto SemanticConstruction::derive_place_uses() noexcept -> void {
    const auto move_child_use = [&](HIRExprID parent,
                                    HIRExprID child) noexcept -> std::optional<SemanticPlaceUse> {
        if (child.index() >= parent.index()) {
            invariant_violation("semantic expression children must precede their owner");
        }
        auto result = std::move(expression_place_uses[child.index()]);
        expression_place_uses[child.index()].reset();
        return result;
    };

    for (auto index = 0uz; index < storage.expressions.size(); ++index) {
        const auto id = HIRExprID::from_index(static_cast<std::uint32_t>(index));
        const auto& expression = storage.expressions.get(id);
        if (const auto* name = std::get_if<HIRNameExpr>(&expression.value)) {
            const auto place = symbol(name->symbol).place;
            if (place.has_value()) {
                expression_place_uses[index] = SemanticPlaceUse {
                    .root = *place,
                    .projections = {},
                    .access = SemanticPlaceAccess::Read,
                };
            }
            continue;
        }
        if (const auto* member = std::get_if<HIRMemberExpr>(&expression.value)) {
            const auto* field = std::get_if<HIRStructFieldTarget>(&member->target);
            if (field == nullptr) {
                continue;
            }
            auto use = move_child_use(id, member->operand_id);
            if (!use.has_value()) {
                continue;
            }
            use->projections.push_back(
                SemanticFieldProjection {
                    .owner = field->owner,
                    .field_index = field->index,
                    .result_type = expression.type,
                }
            );
            expression_place_uses[index] = std::move(use);
            continue;
        }
        if (const auto* indexed = std::get_if<HIRIndexExpr>(&expression.value)) {
            auto use = move_child_use(id, indexed->operand_id);
            if (!use.has_value()) {
                continue;
            }
            use->projections.push_back(SemanticIndexProjection {.result_type = expression.type});
            expression_place_uses[index] = std::move(use);
            continue;
        }
        if (const auto* cast = std::get_if<HIRCastExpr>(&expression.value);
            cast != nullptr && cast->kind == HIRCastKind::Identity) {
            expression_place_uses[index] = move_child_use(id, cast->operand_id);
            continue;
        }
        if (const auto* take = std::get_if<HIRTakeExpr>(&expression.value)) {
            expression_place_uses[index] = move_child_use(id, take->operand_id);
            if (expression_place_uses[index].has_value()) {
                expression_place_uses[index]->access = SemanticPlaceAccess::Take;
            }
        }
    }

    const auto set_access = [&](HIRExprID id, SemanticPlaceAccess access) noexcept {
        auto& use = expression_place_uses[id.index()];
        if (use.has_value()) {
            use->access = access;
        }
    };
    for (const auto& statement : storage.statements.values()) {
        if (const auto* assignment = std::get_if<HIRAssignmentStmt>(&statement.value)) {
            set_access(
                assignment->target,
                assignment->op == HIRAssignmentOperator::Assign ? SemanticPlaceAccess::Write
                                                                : SemanticPlaceAccess::ReadWrite
            );
        } else if (const auto* update = std::get_if<HIRUpdateStmt>(&statement.value)) {
            set_access(update->target, SemanticPlaceAccess::ReadWrite);
        }
    }
    for (const auto& expression : storage.expressions.values()) {
        if (const auto* call = std::get_if<HIRCallExpr>(&expression.value)) {
            for (const auto& argument : call->arguments) {
                if (argument.access != HIRAccessMode::Read) {
                    set_access(
                        argument.expression,
                        argument.access == HIRAccessMode::Write ? SemanticPlaceAccess::Write
                                                                : SemanticPlaceAccess::Take
                    );
                }
            }
        }
    }
}

auto SemanticConstruction::place_use(HIRExprID expression) noexcept
    -> std::optional<SemanticPlaceUse>& {
    if (expression.index() >= expression_place_uses.size()) {
        invariant_violation("place-use state lookup references an unknown expression");
    }
    return expression_place_uses[expression.index()];
}

auto SemanticConstruction::place_use(HIRExprID expression) const noexcept
    -> const std::optional<SemanticPlaceUse>& {
    if (expression.index() >= expression_place_uses.size()) {
        invariant_violation("place-use state lookup references an unknown expression");
    }
    return expression_place_uses[expression.index()];
}

auto SemanticConstruction::publish_control_facts(
    std::vector<HIRExpressionFacts> expressions,
    std::vector<HIRBlockFacts> blocks
) noexcept -> void {
    if (!storage.expression_facts.empty()
        || !storage.block_facts.empty()
        || expressions.size() != storage.expressions.size()
        || blocks.size() != storage.blocks.size()) {
        invariant_violation(
            "semantic control facts were not committed exactly once and completely"
        );
    }
    for (auto& facts : expressions) {
        storage.expression_facts.add(std::move(facts));
    }
    for (auto& facts : blocks) {
        storage.block_facts.add(std::move(facts));
    }
    expression_place_uses.clear();
}

auto SemanticConstruction::normalize_void_block(HIRBlockID id) noexcept -> HIRBlockID {
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

auto SemanticConstruction::publish_declaration_contracts(
    std::vector<HIRFunctionDecl> functions,
    std::vector<HIRStructDecl> structures,
    std::vector<HIREnumDecl> enumerations,
    std::vector<HIREnumCase> enum_cases
) noexcept -> void {
    if (!storage.functions.empty()
        || !storage.structures.empty()
        || !storage.enumerations.empty()
        || !storage.enum_cases.empty()) {
        invariant_violation("declaration contracts were committed more than once");
    }
    for (auto& function : functions) {
        storage.functions.add(std::move(function));
    }
    for (auto& structure : structures) {
        storage.structures.add(std::move(structure));
    }
    for (auto& enumeration : enumerations) {
        storage.enumerations.add(std::move(enumeration));
    }
    for (auto& enum_case : enum_cases) {
        storage.enum_cases.add(std::move(enum_case));
    }
}

auto SemanticConstruction::define_callable_body(
    CallableID callable,
    std::vector<HIRParameter> parameters,
    SemanticScopeID scope,
    HIRBlockID root
) noexcept -> void {
    if (!storage.callables.contains(callable)) {
        invariant_violation("callable body definition references an unknown callable");
    }
    const auto body_id = storage.callables.get(callable).body;
    auto& slot = body_slots[body_id.index()];
    if (slot.has_value()) {
        invariant_violation("semantic body was defined more than once");
    }
    slot = HIRBody {
        .parameters = std::move(parameters),
        .scope = scope,
        .root = root,
    };
}

auto SemanticConstruction::append_test(
    ProgramOriginID origin,
    ProgramSpellingID name,
    SemanticScopeID scope,
    HIRBlockID root
) noexcept -> TestID {
    if (body_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic bodies exhausted their 32-bit identity space");
    }
    const auto body_id = BodyID::from_index(static_cast<std::uint32_t>(body_slots.size()));
    body_slots.emplace_back();
    const auto test_id = storage.tests.add({
        .origin = origin,
        .name = name,
        .body = body_id,
    });
    body_slots[body_id.index()] = HIRBody {
        .parameters = {},
        .scope = scope,
        .root = root,
    };
    return test_id;
}

auto SemanticConstruction::append_module(HIRModule value) noexcept -> ProgramModuleID {
    return storage.modules.add(std::move(value));
}

auto SemanticConstruction::expression(HIRExprID id) noexcept -> HIRExpr& {
    return storage.expressions.get(id);
}

auto SemanticConstruction::statement(HIRStmtID id) noexcept -> HIRStmt& {
    return storage.statements.get(id);
}

auto SemanticConstruction::block(HIRBlockID id) noexcept -> HIRBlock& {
    return storage.blocks.get(id);
}

auto SemanticConstruction::body(BodyID id) noexcept -> HIRBody& {
    if (id.index() >= body_slots.size() || !body_slots[id.index()].has_value()) {
        invariant_violation("semantic body lookup used an undefined identity");
    }
    return *body_slots[id.index()];
}

auto SemanticConstruction::hir_module(ProgramModuleID id) noexcept -> HIRModule& {
    return storage.modules.get(id);
}

auto SemanticConstruction::set_callable_failures(CallableID id, FailureSetID failures) noexcept
    -> void {
    if (!storage.failure_sets.contains(failures)) {
        invariant_violation("callable failure inference references an invalid failure set");
    }
    storage.callables.get(id).failure_set = failures;
}

auto SemanticConstruction::publish_nominal_storage(HIRNominalStorage value) noexcept -> void {
    if (!storage.nominal_storage.order.empty()
        || !storage.nominal_storage.direct_dependencies.empty()) {
        invariant_violation("nominal storage was published more than once");
    }
    if (value.direct_dependencies.size()
        != storage.structures.size() + storage.enumerations.size()) {
        invariant_violation("nominal storage dependencies are incomplete");
    }
    storage.nominal_storage = std::move(value);
}

auto SemanticConstruction::type(HIRTypeID id) const noexcept -> const HIRType& {
    return storage.types.get(id);
}

auto SemanticConstruction::expression(HIRExprID id) const noexcept -> const HIRExpr& {
    return storage.expressions.get(id);
}

auto SemanticConstruction::expression_facts(HIRExprID id) const noexcept
    -> const HIRExpressionFacts& {
    return storage.expression_facts.get(id);
}

auto SemanticConstruction::constant(HIRConstantID id) const noexcept -> const HIRConstantFact& {
    return storage.constants.get(id);
}

auto SemanticConstruction::statement(HIRStmtID id) const noexcept -> const HIRStmt& {
    return storage.statements.get(id);
}

auto SemanticConstruction::pattern(HIRPatternID id) const noexcept -> const HIRPattern& {
    return storage.patterns.get(id);
}

auto SemanticConstruction::block(HIRBlockID id) const noexcept -> const HIRBlock& {
    return storage.blocks.get(id);
}

auto SemanticConstruction::block_facts(HIRBlockID id) const noexcept -> const HIRBlockFacts& {
    return storage.block_facts.get(id);
}

auto SemanticConstruction::scope(SemanticScopeID id) const noexcept -> const SemanticScope& {
    return storage.scopes.get(id);
}

auto SemanticConstruction::place(SemanticPlaceID id) const noexcept -> const SemanticPlace& {
    return storage.places.get(id);
}

auto SemanticConstruction::function(FunctionID id) const noexcept -> const HIRFunctionDecl& {
    return storage.functions.get(id);
}

auto SemanticConstruction::has_body(BodyID id) const noexcept -> bool {
    return id.index() < body_slots.size() && body_slots[id.index()].has_value();
}

auto SemanticConstruction::body(BodyID id) const noexcept -> const HIRBody& {
    if (id.index() >= body_slots.size() || !body_slots[id.index()].has_value()) {
        invariant_violation("semantic body lookup used an undefined identity");
    }
    return *body_slots[id.index()];
}

auto SemanticConstruction::test(TestID id) const noexcept -> const HIRTestDecl& {
    return storage.tests.get(id);
}

auto SemanticConstruction::structure(StructID id) const noexcept -> const HIRStructDecl& {
    return storage.structures.get(id);
}

auto SemanticConstruction::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    return storage.enumerations.get(id);
}

auto SemanticConstruction::enum_case(EnumCaseID id) const noexcept -> const HIREnumCase& {
    return storage.enum_cases.get(id);
}

auto SemanticConstruction::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return symbol_state(id).symbol;
}

auto SemanticConstruction::hir_module(ProgramModuleID id) const noexcept -> const HIRModule& {
    return storage.modules.get(id);
}

auto SemanticConstruction::failure_set(FailureSetID id) const noexcept -> const HIRFailureSet& {
    return storage.failure_sets.get(id);
}

auto SemanticConstruction::callable_signature(CallableSignatureID id) const noexcept
    -> const HIRCallableSignature& {
    return storage.callable_signatures.get(id);
}

auto SemanticConstruction::callable(CallableID id) const noexcept -> const HIRCallable& {
    return storage.callables.get(id);
}

auto SemanticConstruction::functions() const noexcept -> std::span<const HIRFunctionDecl> {
    return storage.functions.values();
}

auto SemanticConstruction::bodies() const noexcept -> std::span<const std::optional<HIRBody>> {
    return body_slots;
}

auto SemanticConstruction::tests() const noexcept -> std::span<const HIRTestDecl> {
    return storage.tests.values();
}

auto SemanticConstruction::structures() const noexcept -> std::span<const HIRStructDecl> {
    return storage.structures.values();
}

auto SemanticConstruction::enumerations() const noexcept -> std::span<const HIREnumDecl> {
    return storage.enumerations.values();
}

auto SemanticConstruction::enum_cases() const noexcept -> std::span<const HIREnumCase> {
    return storage.enum_cases.values();
}

auto SemanticConstruction::modules() const noexcept -> std::span<const HIRModule> {
    return storage.modules.values();
}

auto SemanticConstruction::expressions() const noexcept -> std::span<const HIRExpr> {
    return storage.expressions.values();
}

auto SemanticConstruction::expression_facts() const noexcept
    -> std::span<const HIRExpressionFacts> {
    return storage.expression_facts.values();
}

auto SemanticConstruction::constants() const noexcept -> std::span<const HIRConstantFact> {
    return storage.constants.values();
}

auto SemanticConstruction::types() const noexcept -> std::span<const HIRType> {
    return storage.types.values();
}

auto SemanticConstruction::statements() const noexcept -> std::span<const HIRStmt> {
    return storage.statements.values();
}

auto SemanticConstruction::patterns() const noexcept -> std::span<const HIRPattern> {
    return storage.patterns.values();
}

auto SemanticConstruction::blocks() const noexcept -> std::span<const HIRBlock> {
    return storage.blocks.values();
}

auto SemanticConstruction::block_facts() const noexcept -> std::span<const HIRBlockFacts> {
    return storage.block_facts.values();
}

auto SemanticConstruction::scopes() const noexcept -> std::span<const SemanticScope> {
    return storage.scopes.values();
}

auto SemanticConstruction::places() const noexcept -> std::span<const SemanticPlace> {
    return storage.places.values();
}

auto SemanticConstruction::symbol_count() const noexcept -> std::size_t {
    return symbol_construction.size();
}

auto SemanticConstruction::failure_sets() const noexcept -> std::span<const HIRFailureSet> {
    return storage.failure_sets.values();
}

auto SemanticConstruction::callable_signatures() const noexcept
    -> std::span<const HIRCallableSignature> {
    return storage.callable_signatures.values();
}

auto SemanticConstruction::callables() const noexcept -> std::span<const HIRCallable> {
    return storage.callables.values();
}

auto SemanticConstruction::nominal_storage_order() const noexcept
    -> std::span<const HIRNominalDeclRef> {
    return storage.nominal_storage.order;
}

auto SemanticConstruction::provenance() const noexcept -> CompilationProvenanceView {
    return provenance_builder.view();
}

auto SemanticConstruction::finish() && noexcept -> SemanticProgram {
    if (storage.modules.size() != provenance_builder.module_count()) {
        invariant_violation("semantic modules are not aligned with program provenance");
    }

    if (const auto verified = verify_semantic_program(*this); !verified.has_value()) {
        invariant_violation(verified.error().message);
    }

    for (auto& state : symbol_construction) {
        storage.symbols.add(std::move(state.symbol));
    }
    for (auto& body : body_slots) {
        if (!body.has_value()) {
            invariant_violation("semantic body was not defined before publication");
        }
        storage.bodies.add(std::move(*body));
    }

    return SemanticProgram(std::move(provenance_builder).finish(), std::move(storage));
}
