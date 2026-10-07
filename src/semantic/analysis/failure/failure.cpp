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
            .throw_sites = {},
        }
    );
}

auto FailureConstraintStore::add_union_term(std::vector<ConstructionFailureRef> inputs) noexcept
    -> FailureTermID {
    for (const auto input : inputs) {
        require_source(input);
    }
    return term_table.add(
        FailureTerm {
            .direct_members = {},
            .inputs = std::move(inputs),
            .guarded_inputs = {},
            .excluded_members = {},
            .retained_members = std::nullopt,
            .throw_sites = {},
        }
    );
}

auto FailureConstraintStore::add_residual_term(
    ConstructionFailureRef input,
    std::vector<TypeID> handled_members
) noexcept -> FailureTermID {
    require_source(input);
    return term_table.add(
        FailureTerm {
            .direct_members = {},
            .inputs = {input},
            .guarded_inputs = {},
            .excluded_members = normalize_members(std::move(handled_members)),
            .retained_members = std::nullopt,
            .throw_sites = {},
        }
    );
}

auto FailureConstraintStore::add_intersection_term(
    ConstructionFailureRef input,
    std::vector<TypeID> retained_members
) noexcept -> FailureTermID {
    require_source(input);
    return term_table.add(
        FailureTerm {
            .direct_members = {},
            .inputs = {input},
            .guarded_inputs = {},
            .excluded_members = {},
            .retained_members = normalize_members(std::move(retained_members)),
            .throw_sites = {},
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
    auto& members = term_table.mutate(destination).direct_members;
    const auto position = std::ranges::lower_bound(members, member.index(), {}, &TypeID::index);
    if (position == members.end() || *position != member) {
        members.insert(position, member);
    }
}

auto FailureConstraintStore::add_thrown_member(
    FailureTermID destination,
    TypeID member,
    ProgramOriginID origin
) noexcept -> void {
    add_member(destination, member);
    require_origin(origin);
    term_table.mutate(destination).throw_sites.push_back({.member = member, .origin = origin});
}

auto FailureConstraintStore::add_contribution(
    FailureTermID destination,
    ConstructionFailureRef source
) noexcept -> void {
    require_term(destination);
    require_source(source);
    // Inputs are an unordered set; finish() normalizes each term once.
    term_table.mutate(destination).inputs.push_back(source);
}

auto FailureConstraintStore::add_guarded_contribution(
    FailureTermID destination,
    ConstructionFailureRef gate,
    ConstructionFailureRef source
) noexcept -> void {
    require_term(destination);
    require_source(gate);
    require_source(source);
    term_table.mutate(destination)
        .guarded_inputs.push_back(
            FailureTerm::GuardedContribution {
                .gate = gate,
                .source = source,
            }
        );
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
    ConstructionFailureRef source,
    ProgramOriginID origin,
    EmptyFailureRequirementKind kind
) noexcept -> void {
    require_source(source);
    require_origin(origin);
    requirements.push_back(
        RequiresEmptyFailure {
            .source = source,
            .origin = origin,
            .kind = kind,
        }
    );
}

auto FailureConstraintStore::require_non_empty(
    ConstructionFailureRef source,
    ProgramOriginID origin
) noexcept -> void {
    require_source(source);
    require_origin(origin);
    requirements.push_back(
        RequiresNonEmptyFailure {
            .source = source,
            .origin = origin,
        }
    );
}

auto FailureConstraintStore::require_subset(
    ConstructionFailureRef actual,
    ConstructionFailureRef allowed,
    ProgramOriginID origin,
    FailureSubsetRequirementKind kind
) noexcept -> void {
    require_source(actual);
    require_source(allowed);
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
    ConstructionFailureRef left,
    ConstructionFailureRef right,
    ProgramOriginID origin
) noexcept -> void {
    require_source(left);
    require_source(right);
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
    ConstructionFailureRef actual,
    ProgramOriginID origin
) noexcept -> void {
    require_source(actual);
    require_origin(origin);
    requirements.push_back(
        RequiresDeclaredFailureContract {
            .actual = actual,
            .origin = origin,
        }
    );
}

auto FailureConstraintStore::finish() && noexcept -> FrozenFailureConstraints {
    for (auto& term : term_table.mutable_values()) {
        term.inputs = normalize_inputs(std::move(term.inputs));
        std::ranges::sort(term.guarded_inputs, {}, [](const auto& input) static noexcept {
            return std::pair(input.gate, input.source);
        });
        term.guarded_inputs.erase(
            std::ranges::unique(term.guarded_inputs).begin(),
            term.guarded_inputs.end()
        );
    }
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

auto FailureConstraintStore::require_source(ConstructionFailureRef source) const noexcept -> void {
    if (const auto* term = std::get_if<FailureTermID>(&source)) {
        require_term(*term);
    } else if (std::get<FailureSetID>(source).owner() != program_identity) {
        invariant_violation("failure source belongs to another semantic program");
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

auto FailureConstraintStore::normalize_inputs(
    std::vector<ConstructionFailureRef> inputs
) const noexcept -> std::vector<ConstructionFailureRef> {
    for (const auto input : inputs) {
        require_source(input);
    }
    std::ranges::sort(inputs);
    inputs.erase(std::ranges::unique(inputs).begin(), inputs.end());
    return inputs;
}
