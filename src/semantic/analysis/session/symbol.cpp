module carven:semantic.analysis.session.symbol.impl;

import :semantic.analysis.session;
import :semantic.hir.expr;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :semantic.hir.type;
import :support.invariant;
import :support.visit;
import std;

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
