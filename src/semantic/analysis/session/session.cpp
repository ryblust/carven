module carven:semantic.analysis.session.impl;

import :semantic.analysis.session;
import :semantic.analysis.failures;
import :semantic.analysis.validation.invariants;
import :semantic.hir.constant;
import :semantic.hir.expr;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

SemanticDraft::SemanticDraft(CompilationProvenance provenance) noexcept
    : provenance_builder(std::move(provenance)) {}

SemanticSession::SemanticSession(CompilationProvenance provenance) noexcept
    : semantic_draft(std::move(provenance)) {}

auto SemanticSession::draft() noexcept -> SemanticDraft& {
    return semantic_draft;
}

auto SemanticSession::draft() const noexcept -> const SemanticDraft& {
    return semantic_draft;
}

SemanticEntityReservations::SemanticEntityReservations(SemanticDraft& source) noexcept
    : construction(std::addressof(source)) {}

auto SemanticEntityReservations::reserve_symbol() noexcept -> SymbolID {
    return construction->reserve_symbol();
}

auto SemanticEntityReservations::reserve_function() noexcept -> FunctionID {
    return construction->reserve_function();
}

auto SemanticEntityReservations::reserve_struct() noexcept -> StructID {
    return construction->reserve_struct();
}

auto SemanticEntityReservations::reserve_enum() noexcept -> EnumID {
    return construction->reserve_enum();
}

auto SemanticEntityReservations::reserve_enum_case() noexcept -> EnumCaseID {
    return construction->reserve_enum_case();
}

SemanticDeclarationCapabilities::SemanticDeclarationCapabilities(SemanticDraft& draft) noexcept
    : semantic_draft(std::addressof(draft)) {}

auto SemanticDeclarationCapabilities::define(FunctionID id, HIRFunctionDecl declaration) noexcept
    -> void {
    semantic_draft->define_function_contract(id, std::move(declaration));
}

auto SemanticDeclarationCapabilities::define(StructID id, HIRStructDecl declaration) noexcept
    -> void {
    semantic_draft->define_struct_contract(id, std::move(declaration));
}

auto SemanticDeclarationCapabilities::define(EnumID id, HIREnumDecl declaration) noexcept -> void {
    semantic_draft->define_enum_contract(id, std::move(declaration));
}

auto SemanticDeclarationCapabilities::define(
    EnumCaseID id,
    SemanticEnumCaseContract declaration
) noexcept -> void {
    semantic_draft->define_enum_case_contract(id, std::move(declaration));
}

auto SemanticDeclarationCapabilities::function(FunctionID id) const noexcept
    -> const HIRFunctionDecl& {
    return semantic_draft->function(id);
}

auto SemanticDeclarationCapabilities::structure(StructID id) const noexcept
    -> const HIRStructDecl& {
    return semantic_draft->structure(id);
}

auto SemanticDeclarationCapabilities::enumeration(EnumID id) const noexcept -> const HIREnumDecl& {
    return semantic_draft->enumeration(id);
}

auto SemanticDeclarationCapabilities::enum_case(EnumCaseID id) const noexcept
    -> SemanticEnumCaseView {
    return semantic_draft->enum_case_contract_view(id);
}

auto SemanticDeclarationCapabilities::symbol(SymbolID id) const noexcept -> const HIRSymbol& {
    return semantic_draft->symbol(id);
}

auto SemanticDeclarationCapabilities::type(HIRTypeID id) const noexcept -> const HIRType& {
    return semantic_draft->type(id);
}

auto SemanticDeclarationCapabilities::complete_enum_case(
    EnumCaseID id,
    std::optional<HIRConstantID> constant
) noexcept -> void {
    semantic_draft->complete_enum_case_contract(id, constant);
}

auto SemanticDeclarationCapabilities::seal_declarations(
    std::vector<HIRNominalCapabilities> structures,
    std::vector<HIRNominalCapabilities> enumerations
) noexcept -> void {
    semantic_draft->freeze_nominal_capabilities(std::move(structures), std::move(enumerations));
    semantic_draft->freeze_declaration_contracts();
}

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

auto SemanticDraft::reserve_symbol() noexcept -> SymbolID {
    if (reserved_symbol_count == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic symbols exhausted their 32-bit identity space");
    }
    const auto id = SymbolID::from_index(static_cast<std::uint32_t>(reserved_symbol_count));
    ++reserved_symbol_count;
    return id;
}

auto SemanticDraft::reserve_function() noexcept -> FunctionID {
    if (function_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic functions exhausted their 32-bit identity space");
    }
    const auto id = FunctionID::from_index(static_cast<std::uint32_t>(function_slots.size()));
    function_slots.emplace_back();
    return id;
}

auto SemanticDraft::reserve_struct() noexcept -> StructID {
    if (struct_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic structures exhausted their 32-bit identity space");
    }
    const auto id = StructID::from_index(static_cast<std::uint32_t>(struct_slots.size()));
    struct_slots.emplace_back();
    return id;
}

auto SemanticDraft::reserve_enum() noexcept -> EnumID {
    if (enum_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic enumerations exhausted their 32-bit identity space");
    }
    const auto id = EnumID::from_index(static_cast<std::uint32_t>(enum_slots.size()));
    enum_slots.emplace_back();
    return id;
}

auto SemanticDraft::reserve_enum_case() noexcept -> EnumCaseID {
    if (enum_case_slots.size() == std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("semantic enum cases exhausted their 32-bit identity space");
    }
    const auto id = EnumCaseID::from_index(static_cast<std::uint32_t>(enum_case_slots.size()));
    enum_case_slots.emplace_back();
    return id;
}

auto SemanticDraft::append_symbol(SemanticSymbolSpec spec) noexcept -> SymbolID {
    const auto symbol = reserve_symbol();
    define_reserved_symbol(symbol, std::move(spec));
    return symbol;
}

auto SemanticDraft::define_reserved_symbol(SymbolID symbol, SemanticSymbolSpec spec) noexcept
    -> void {
    if (symbol.index() != symbol_construction.size() || symbol.index() >= reserved_symbol_count) {
        invariant_violation("semantic symbol definition does not match its reserved identity");
    }
    symbol_construction.push_back({
        .symbol =
            {
                .name = spec.name,
                .module_id = spec.module_id,
                .type = std::nullopt,
                .parent = spec.parent,
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
}

auto SemanticDraft::symbol_state(SymbolID symbol) noexcept -> SemanticSymbolState& {
    if (symbol.index() >= symbol_construction.size()) {
        invariant_violation("semantic symbol construction lookup used an invalid identity");
    }
    return symbol_construction[symbol.index()];
}

auto SemanticDraft::symbol_state(SymbolID symbol) const noexcept -> const SemanticSymbolState& {
    if (symbol.index() >= symbol_construction.size()) {
        invariant_violation("semantic symbol construction lookup used an invalid identity");
    }
    return symbol_construction[symbol.index()];
}

auto SemanticDraft::adopt_symbol_type(SymbolID symbol, HIRTypeID type) noexcept -> void {
    auto& state = symbol_state(symbol);
    if (state.symbol.type.has_value()) {
        invariant_violation("semantic symbol type was defined more than once");
    }
    state.symbol.type = type;
}

auto SemanticDraft::define_symbol_constant(SymbolID symbol, HIRConstantID constant) noexcept
    -> void {
    auto& state = symbol_state(symbol);
    if (state.constant.has_value()) {
        invariant_violation("semantic symbol constant was defined more than once");
    }
    state.constant = constant;
}

auto SemanticDraft::define_symbol_binding(
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

auto SemanticDraft::mark_symbol_referenced(SymbolID symbol) noexcept -> void {
    symbol_state(symbol).symbol.referenced = true;
}

auto SemanticDraft::mark_explicit_capture(SymbolID symbol) noexcept -> void {
    symbol_state(symbol).explicit_capture = true;
}

auto SemanticDraft::record_symbol_lint_candidate(SymbolID symbol, ProgramOriginID origin) noexcept
    -> void {
    auto& state = symbol_state(symbol);
    if (state.unused_candidate.has_value()) {
        invariant_violation("semantic symbol lint candidate was recorded more than once");
    }
    state.unused_candidate = origin;
}

auto SemanticDraft::symbol_constant(SymbolID symbol) const noexcept
    -> std::optional<HIRConstantID> {
    return symbol_state(symbol).constant;
}

auto SemanticDraft::symbol_role(SymbolID symbol) const noexcept -> SemanticSymbolRole {
    return symbol_state(symbol).role;
}

auto SemanticDraft::symbol_write_eligible(SymbolID symbol) const noexcept -> bool {
    return symbol_state(symbol).write_eligible;
}

auto SemanticDraft::symbol_states() const noexcept -> std::span<const SemanticSymbolState> {
    return symbol_construction;
}

auto SemanticDraft::bind_symbol(SymbolID symbol, SemanticBindingPosition position) noexcept
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

auto SemanticDraft::make_binding_facts(
    SymbolID symbol,
    SemanticBindingStorage storage_kind,
    SemanticBindingCapabilities capabilities
) const noexcept -> SemanticBindingFacts {
    const auto& state = symbol_state(symbol);
    if (!state.binding_position.has_value()) {
        invariant_violation("runtime binding has no lexical position");
    }
    if (!state.symbol.type.has_value()) {
        invariant_violation("runtime binding has no type");
    }
    const auto position = *state.binding_position;
    return {
        .storage = storage_kind,
        .capabilities = capabilities,
        .scope = position.scope,
        .declaration_order = position.declaration_order,
    };
}

auto SemanticDraft::derive_binding_facts() noexcept -> void {
    if (!storage.bindings.empty()) {
        invariant_violation("semantic binding facts were derived more than once");
    }
    for (auto index = 0uz; index < symbol_construction.size(); ++index) {
        const auto symbol = SymbolID::from_index(static_cast<std::uint32_t>(index));
        const auto& state = symbol_construction[index];
        auto facts = std::optional<SemanticBindingFacts>();
        switch (state.binding_role) {
            case SemanticBindingRole::None:
            case SemanticBindingRole::CompileTime: break;
            case SemanticBindingRole::Owner:
                facts = make_binding_facts(
                    symbol,
                    SemanticBindingStorage::Owner,
                    {.write = state.write_eligible, .take = true}
                );
                break;
            case SemanticBindingRole::ReadAlias:
                facts = make_binding_facts(
                    symbol,
                    SemanticBindingStorage::Borrow,
                    {.write = false, .take = false}
                );
                break;
            case SemanticBindingRole::WriteAlias:
                facts = make_binding_facts(
                    symbol,
                    SemanticBindingStorage::Borrow,
                    {.write = true, .take = false}
                );
                break;
            case SemanticBindingRole::ClosureState:
                facts = make_binding_facts(
                    symbol,
                    SemanticBindingStorage::Compiler,
                    {.write = false, .take = false}
                );
                break;
        }
        if (storage.bindings.add(std::move(facts)) != symbol) {
            invariant_violation("semantic binding facts are not aligned with symbols");
        }
    }
}

auto SemanticDraft::derive_place_uses() noexcept -> void {
    if (storage.bindings.size() != symbol_construction.size()) {
        invariant_violation("place-use analysis requires complete symbol binding facts");
    }
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
            if (binding(name->symbol).has_value()) {
                expression_place_uses[index] = SemanticPlaceUse {
                    .root = name->symbol,
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
            use->projections.push_back(SemanticIndexProjection {});
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

auto SemanticDraft::place_use_candidate(HIRExprID expression) noexcept
    -> std::optional<SemanticPlaceUse>& {
    if (expression.index() >= expression_place_uses.size()) {
        invariant_violation("place-use state lookup references an unknown expression");
    }
    return expression_place_uses[expression.index()];
}

auto SemanticDraft::place_use_candidate(HIRExprID expression) const noexcept
    -> const std::optional<SemanticPlaceUse>& {
    if (expression.index() >= expression_place_uses.size()) {
        invariant_violation("place-use state lookup references an unknown expression");
    }
    return expression_place_uses[expression.index()];
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

auto SemanticDraft::define_function_contract(FunctionID id, HIRFunctionDecl declaration) noexcept
    -> void {
    if (id.index() >= function_slots.size() || function_slots[id.index()].has_value()) {
        invariant_violation("function contract was not defined exactly once in its reserved slot");
    }
    function_slots[id.index()] = std::move(declaration);
}

auto SemanticDraft::define_struct_contract(StructID id, HIRStructDecl declaration) noexcept
    -> void {
    if (id.index() >= struct_slots.size() || struct_slots[id.index()].has_value()) {
        invariant_violation("struct contract was not defined exactly once in its reserved slot");
    }
    struct_slots[id.index()] = std::move(declaration);
}

auto SemanticDraft::define_enum_contract(EnumID id, HIREnumDecl declaration) noexcept -> void {
    if (id.index() >= enum_slots.size() || enum_slots[id.index()].has_value()) {
        invariant_violation("enum contract was not defined exactly once in its reserved slot");
    }
    enum_slots[id.index()] = std::move(declaration);
}

auto SemanticDraft::define_enum_case_contract(
    EnumCaseID id,
    SemanticEnumCaseContract declaration
) noexcept -> void {
    if (id.index() >= enum_case_slots.size() || enum_case_slots[id.index()].has_value()) {
        invariant_violation("enum-case contract was not defined exactly once in its reserved slot");
    }
    enum_case_slots[id.index()] = EnumCaseSlot {
        .contract = std::move(declaration),
        .constant = std::nullopt,
    };
}

auto SemanticDraft::complete_enum_case_contract(
    EnumCaseID id,
    std::optional<HIRConstantID> constant
) noexcept -> void {
    if (id.index() >= enum_case_slots.size() || !enum_case_slots[id.index()].has_value()) {
        invariant_violation("enum-case constant completion used an unresolved contract");
    }
    auto& completion = enum_case_slots[id.index()]->constant;
    if (completion.has_value()) {
        invariant_violation("enum-case constant was completed more than once");
    }
    completion = constant;
}

auto SemanticDraft::enum_case_contract_view(EnumCaseID id) const noexcept -> SemanticEnumCaseView {
    if (storage.enum_cases.contains(id)) {
        const auto& declaration = storage.enum_cases.get(id);
        return {
            .owner = declaration.owner,
            .name = declaration.name,
            .payload_types = declaration.payload_types,
            .symbol = declaration.symbol,
            .origin = declaration.origin,
        };
    }
    if (id.index() >= enum_case_slots.size() || !enum_case_slots[id.index()].has_value()) {
        invariant_violation("enum-case contract lookup used an unresolved reserved identity");
    }
    const auto& declaration = enum_case_slots[id.index()]->contract;
    return {
        .owner = declaration.owner,
        .name = declaration.name,
        .payload_types = declaration.payload_types,
        .symbol = declaration.symbol,
        .origin = declaration.origin,
    };
}

auto SemanticDraft::freeze_nominal_capabilities(
    std::vector<HIRNominalCapabilities> structures,
    std::vector<HIRNominalCapabilities> enumerations
) noexcept -> void {
    if (!storage.struct_capabilities.empty()
        || !storage.enum_capabilities.empty()
        || structures.size() != struct_slots.size()
        || enumerations.size() != enum_slots.size()) {
        invariant_violation("nominal capabilities were not frozen exactly once and completely");
    }
    for (auto& capabilities : structures) {
        storage.struct_capabilities.add(std::move(capabilities));
    }
    for (auto& capabilities : enumerations) {
        storage.enum_capabilities.add(std::move(capabilities));
    }
}

auto SemanticDraft::freeze_declaration_contracts() noexcept -> void {
    if (!storage.functions.empty()
        || !storage.structures.empty()
        || !storage.enumerations.empty()
        || !storage.enum_cases.empty()) {
        invariant_violation("declaration contracts were frozen more than once");
    }
    const auto move_slots = [](auto& slots, auto& destination) static noexcept {
        for (auto& slot : slots) {
            if (!slot.has_value()) {
                invariant_violation("required declaration contract slot is unresolved");
            }
            destination.add(std::move(*slot));
        }
        slots.clear();
    };
    move_slots(function_slots, storage.functions);
    move_slots(struct_slots, storage.structures);
    move_slots(enum_slots, storage.enumerations);
    for (auto& slot : enum_case_slots) {
        if (!slot.has_value() || !slot->constant.has_value()) {
            invariant_violation("required enum-case contract slot is incomplete");
        }
        auto& contract = slot->contract;
        storage.enum_cases.add({
            .owner = contract.owner,
            .name = contract.name,
            .payload_types = std::move(contract.payload_types),
            .constant = std::move(*slot->constant),
            .symbol = contract.symbol,
            .origin = contract.origin,
        });
    }
    enum_case_slots.clear();
}

auto SemanticDraft::define_callable_body(
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

auto SemanticDraft::append_test(
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

auto SemanticSession::finish() && noexcept -> SemanticProgram {
    auto& draft = semantic_draft;
    if (draft.storage.modules.size() != draft.provenance_builder.module_count()) {
        invariant_violation("semantic modules are not aligned with program provenance");
    }
    if (draft.symbol_construction.size() != draft.reserved_symbol_count
        || !draft.function_slots.empty()
        || !draft.struct_slots.empty()
        || !draft.enum_slots.empty()
        || !draft.enum_case_slots.empty()) {
        invariant_violation("semantic reserved entity slots were not completed before seal");
    }
    if (!draft.expression_place_uses.empty() || !draft.callable_failure_input_storage.empty()) {
        invariant_violation("transient semantic analysis inputs survived flow freeze");
    }

    draft.storage.type_index.clear();
    draft.storage.failure_set_index.clear();
    draft.storage.callable_signature_index.clear();

    auto published = SemanticProgramStorage();
    published.types = std::move(draft.storage.types);
    published.expressions = std::move(draft.storage.expressions);
    published.expression_controls = std::move(draft.storage.expression_controls);
    published.evaluation_effects = std::move(draft.storage.evaluation_effects);
    published.place_uses = std::move(draft.storage.place_uses);
    published.try_facts = std::move(draft.storage.try_facts);
    published.constants = std::move(draft.storage.constants);
    published.statements = std::move(draft.storage.statements);
    published.patterns = std::move(draft.storage.patterns);
    published.blocks = std::move(draft.storage.blocks);
    published.block_controls = std::move(draft.storage.block_controls);
    published.scopes = std::move(draft.storage.scopes);
    published.bindings = std::move(draft.storage.bindings);
    published.functions = std::move(draft.storage.functions);
    published.tests = std::move(draft.storage.tests);
    published.structures = std::move(draft.storage.structures);
    published.enumerations = std::move(draft.storage.enumerations);
    published.enum_cases = std::move(draft.storage.enum_cases);
    published.modules = std::move(draft.storage.modules);
    published.failure_sets = std::move(draft.storage.failure_sets);
    published.callable_signatures = std::move(draft.storage.callable_signatures);
    published.callables = std::move(draft.storage.callables);
    published.callable_flows = std::move(draft.storage.callable_flows);
    published.struct_capabilities = std::move(draft.storage.struct_capabilities);
    published.enum_capabilities = std::move(draft.storage.enum_capabilities);
    published.nominal_containment = std::move(draft.storage.nominal_containment);

    for (auto& state : draft.symbol_construction) {
        published.symbols.add(std::move(state.symbol));
    }
    draft.symbol_construction.clear();
    for (auto& body : draft.body_slots) {
        if (!body.has_value()) {
            invariant_violation("semantic body was not defined before publication");
        }
        published.bodies.add(std::move(*body));
    }
    draft.body_slots.clear();

    auto provenance = std::move(draft.provenance_builder).finish();
    const auto published_view = SemanticProgramView(provenance.view(), published);
    if (const auto verified = verify_semantic_program(published_view); !verified.has_value()) {
        invariant_violation(verified.error().message);
    }
    return SemanticProgram(std::move(provenance), std::move(published));
}
