module carven:semantic.analysis.failure.impl;

import :semantic.analysis.failure;
import :support.invariant;
import std;

FrozenFailureConstraints::FrozenFailureConstraints(
    ImmutableProgramTable<FailureTerm, FailureTermID> terms,
    std::vector<FailureConstraint> constraints,
    ProvenanceIdentity provenance
) noexcept
    : term_table(std::move(terms)),
      requirements(std::move(constraints)),
      provenance_identity(provenance) {}

auto FrozenFailureConstraints::owner() const noexcept -> ProgramIdentity {
    return term_table.owner();
}

auto FrozenFailureConstraints::provenance_owner() const noexcept -> ProvenanceIdentity {
    return provenance_identity;
}

auto FrozenFailureConstraints::terms() const noexcept
    -> const ImmutableProgramTable<FailureTerm, FailureTermID>& {
    return term_table;
}

auto FrozenFailureConstraints::constraints() const noexcept -> std::span<const FailureConstraint> {
    return requirements;
}

FailureConstraintStore::FailureConstraintStore(
    ProgramIdentity owner,
    ProvenanceIdentity provenance
) noexcept
    : program_identity(owner),
      provenance_identity(provenance),
      term_table(owner) {}

auto FailureConstraintStore::add_empty_term() noexcept -> FailureTermID {
    return term_table.add(FailureTerm {});
}

auto FailureConstraintStore::add_concrete_term(std::vector<TypeID> members) noexcept
    -> FailureTermID {
    return term_table.add(
        FailureTerm {
            .direct_members = normalize_members(std::move(members)),
            .inputs = {},
            .guarded_inputs = {},
            .excluded_members = {},
            .retained_members = std::nullopt,
        }
    );
}

auto FailureConstraintStore::add_union_term(std::vector<FailureTermID> inputs) noexcept
    -> FailureTermID {
    return term_table.add(
        FailureTerm {
            .direct_members = {},
            .inputs = normalize_inputs(std::move(inputs)),
            .guarded_inputs = {},
            .excluded_members = {},
            .retained_members = std::nullopt,
        }
    );
}

auto FailureConstraintStore::add_residual_term(
    FailureTermID input,
    std::vector<TypeID> handled_members
) noexcept -> FailureTermID {
    require_term(input);
    return term_table.add(
        FailureTerm {
            .direct_members = {},
            .inputs = {input},
            .guarded_inputs = {},
            .excluded_members = normalize_members(std::move(handled_members)),
            .retained_members = std::nullopt,
        }
    );
}

auto FailureConstraintStore::add_intersection_term(
    FailureTermID input,
    std::vector<TypeID> retained_members
) noexcept -> FailureTermID {
    require_term(input);
    return term_table.add(
        FailureTerm {
            .direct_members = {},
            .inputs = {input},
            .guarded_inputs = {},
            .excluded_members = {},
            .retained_members = normalize_members(std::move(retained_members)),
        }
    );
}

auto FailureConstraintStore::copy(FailureTermID term) const noexcept -> FailureTerm {
    require_term(term);
    return term_table.copy(term);
}

auto FailureConstraintStore::add_member(FailureTermID destination, TypeID member) noexcept -> void {
    require_term(destination);
    if (member.owner() != program_identity) {
        invariant_violation("failure term member belongs to another semantic program");
    }
    auto term = term_table.copy(destination);
    term.direct_members.push_back(member);
    term.direct_members = normalize_members(std::move(term.direct_members));
    term_table.replace(destination, std::move(term));
}

auto FailureConstraintStore::add_contribution(
    FailureTermID destination,
    FailureTermID source
) noexcept -> void {
    require_term(destination);
    require_term(source);
    auto term = term_table.copy(destination);
    term.inputs.push_back(source);
    term.inputs = normalize_inputs(std::move(term.inputs));
    term_table.replace(destination, std::move(term));
}

auto FailureConstraintStore::add_guarded_contribution(
    FailureTermID destination,
    FailureTermID gate,
    FailureTermID source
) noexcept -> void {
    require_term(destination);
    require_term(gate);
    require_term(source);
    auto term = term_table.copy(destination);
    term.guarded_inputs.push_back(
        FailureTerm::GuardedContribution {
            .gate = gate,
            .source = source,
        }
    );
    std::ranges::sort(term.guarded_inputs, [](const auto& left, const auto& right) static noexcept {
        return std::tuple(left.gate.index(), left.source.index())
            < std::tuple(right.gate.index(), right.source.index());
    });
    term.guarded_inputs.erase(
        std::ranges::unique(term.guarded_inputs).begin(),
        term.guarded_inputs.end()
    );
    term_table.replace(destination, std::move(term));
}

auto FailureConstraintStore::equate(FailureTermID left, FailureTermID right) noexcept -> void {
    require_term(left);
    require_term(right);
    if (left == right) {
        return;
    }
    const auto left_term = term_table.copy(left);
    const auto right_term = term_table.copy(right);
    if (!left_term.excluded_members.empty()
        || left_term.retained_members.has_value()
        || !right_term.excluded_members.empty()
        || right_term.retained_members.has_value()) {
        invariant_violation("filtered failure terms cannot participate in an equality constraint");
    }
    add_contribution(left, right);
    add_contribution(right, left);
}

auto FailureConstraintStore::require_empty(
    FailureTermID term,
    ProgramOriginID origin,
    EmptyFailureRequirementKind kind
) noexcept -> void {
    require_term(term);
    require_origin(origin);
    requirements.push_back(
        RequiresEmptyFailure {
            .term = term,
            .origin = origin,
            .kind = kind,
        }
    );
}

auto FailureConstraintStore::require_non_empty(FailureTermID term, ProgramOriginID origin) noexcept
    -> void {
    require_term(term);
    require_origin(origin);
    requirements.push_back(
        RequiresNonEmptyFailure {
            .term = term,
            .origin = origin,
        }
    );
}

auto FailureConstraintStore::require_subset(
    FailureTermID actual,
    FailureTermID allowed,
    ProgramOriginID origin,
    FailureSubsetRequirementKind kind
) noexcept -> void {
    require_term(actual);
    require_term(allowed);
    require_origin(origin);
    requirements.push_back(
        RequiresFailureSubset {
            .actual = actual,
            .allowed = allowed,
            .origin = origin,
            .kind = kind,
        }
    );
}

auto FailureConstraintStore::require_equal(
    FailureTermID left,
    FailureTermID right,
    ProgramOriginID origin
) noexcept -> void {
    require_term(left);
    require_term(right);
    require_origin(origin);
    requirements.push_back(
        RequiresEqualFailures {
            .left = left,
            .right = right,
            .origin = origin,
        }
    );
}

auto FailureConstraintStore::require_declared_contract(
    FailureTermID actual,
    ProgramOriginID origin
) noexcept -> void {
    require_term(actual);
    require_origin(origin);
    requirements.push_back(
        RequiresDeclaredFailureContract {
            .actual = actual,
            .origin = origin,
        }
    );
}

auto FailureConstraintStore::finish() && noexcept -> FrozenFailureConstraints {
    return FrozenFailureConstraints(
        std::move(term_table).seal(),
        std::move(requirements),
        provenance_identity
    );
}

auto FailureConstraintStore::require_term(FailureTermID term) const noexcept -> void {
    if (!term_table.contains(term)) {
        invariant_violation("failure constraint used a foreign or invalid term identity");
    }
}

auto FailureConstraintStore::require_origin(ProgramOriginID origin) const noexcept -> void {
    if (origin.owner() != provenance_identity) {
        invariant_violation("failure constraint used an origin from another provenance owner");
    }
}

auto FailureConstraintStore::normalize_members(std::vector<TypeID> members) const noexcept
    -> std::vector<TypeID> {
    if (std::ranges::any_of(members, [&](TypeID member) noexcept {
            return member.owner() != program_identity;
        })) {
        invariant_violation("failure set contains a type from another semantic program");
    }
    std::ranges::sort(members, {}, &TypeID::index);
    members.erase(std::ranges::unique(members).begin(), members.end());
    return members;
}

auto FailureConstraintStore::normalize_inputs(std::vector<FailureTermID> inputs) const noexcept
    -> std::vector<FailureTermID> {
    for (const auto input : inputs) {
        require_term(input);
    }
    std::ranges::sort(inputs, {}, &FailureTermID::index);
    inputs.erase(std::ranges::unique(inputs).begin(), inputs.end());
    return inputs;
}
