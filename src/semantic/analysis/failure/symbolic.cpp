module carven:semantic.analysis.failure.symbolic.impl;

import :semantic.analysis.failure;
import :support.invariant;
import std;

namespace {

enum class MemberOperation { Union, Intersection, Difference };

auto combine_members(
    std::span<const TypeID> left,
    std::span<const TypeID> right,
    MemberOperation operation
) noexcept -> std::vector<TypeID> {
    auto result = std::vector<TypeID>();
    switch (operation) {
        case MemberOperation::Union:
            std::ranges::set_union(left, right, std::back_inserter(result));
            break;
        case MemberOperation::Intersection:
            std::ranges::set_intersection(left, right, std::back_inserter(result));
            break;
        case MemberOperation::Difference:
            std::ranges::set_difference(left, right, std::back_inserter(result));
            break;
    }
    return result;
}

auto union_masks(const GenericFailureMask& left, const GenericFailureMask& right) noexcept
    -> GenericFailureMask {
    if (left.complemented && right.complemented) {
        return {
            .complemented = true,
            .members = combine_members(left.members, right.members, MemberOperation::Intersection)
        };
    }
    if (left.complemented || right.complemented) {
        const auto& complement = left.complemented ? left : right;
        const auto& finite = left.complemented ? right : left;
        return {
            .complemented = true,
            .members =
                combine_members(complement.members, finite.members, MemberOperation::Difference)
        };
    }
    return {
        .complemented = false,
        .members = combine_members(left.members, right.members, MemberOperation::Union)
    };
}

auto intersect_masks(const GenericFailureMask& left, const GenericFailureMask& right) noexcept
    -> GenericFailureMask {
    if (left.complemented && right.complemented) {
        return {
            .complemented = true,
            .members = combine_members(left.members, right.members, MemberOperation::Union)
        };
    }
    if (left.complemented || right.complemented) {
        const auto& complement = left.complemented ? left : right;
        const auto& finite = left.complemented ? right : left;
        return {
            .complemented = false,
            .members =
                combine_members(finite.members, complement.members, MemberOperation::Difference)
        };
    }
    return {
        .complemented = false,
        .members = combine_members(left.members, right.members, MemberOperation::Intersection)
    };
}

auto mask_subset(const GenericFailureMask& left, const GenericFailureMask& right) noexcept -> bool {
    if (!left.complemented && !right.complemented) {
        return std::ranges::includes(right.members, left.members);
    }
    if (!left.complemented && right.complemented) {
        return combine_members(left.members, right.members, MemberOperation::Intersection).empty();
    }
    if (left.complemented && !right.complemented) {
        return false;
    }
    return std::ranges::includes(left.members, right.members);
}

auto merge_expression(
    GenericFailureExpression& destination,
    GenericFailureExpression source
) noexcept -> void {
    destination.concrete_members = combine_members(
        destination.concrete_members,
        source.concrete_members,
        MemberOperation::Union
    );
    for (auto& component : source.parameters) {
        const auto position = std::ranges::lower_bound(
            destination.parameters,
            component.parameter,
            {},
            &GenericFailureComponent::parameter
        );
        if (position != destination.parameters.end()
            && position->parameter == component.parameter) {
            position->mask = union_masks(position->mask, component.mask);
        } else {
            destination.parameters.insert(position, std::move(component));
        }
    }
    destination.guards.insert(destination.guards.end(), source.guards.begin(), source.guards.end());
}

auto normal_subset(
    const GenericFailureExpression& actual,
    const GenericFailureExpression& allowed
) noexcept -> bool {
    if (!std::ranges::includes(allowed.concrete_members, actual.concrete_members)) {
        return false;
    }
    for (const auto& component : actual.parameters) {
        const auto position = std::ranges::lower_bound(
            allowed.parameters,
            component.parameter,
            {},
            &GenericFailureComponent::parameter
        );
        auto permitted = GenericFailureMask {
            .complemented = false,
            .members = allowed.concrete_members,
        };
        if (position != allowed.parameters.end() && position->parameter == component.parameter) {
            permitted = union_masks(permitted, position->mask);
        }
        if (!mask_subset(component.mask, permitted)) {
            return false;
        }
    }
    return true;
}

} // namespace

auto intersect_failure_masks(GenericFailureMask left, GenericFailureMask right) noexcept
    -> GenericFailureMask {
    return intersect_masks(left, right);
}

auto residual_failure_mask(GenericFailureMask source, std::vector<TypeID> excluded) noexcept
    -> GenericFailureMask {
    std::ranges::sort(excluded);
    excluded.erase(std::ranges::unique(excluded).begin(), excluded.end());
    return intersect_masks(source, {.complemented = true, .members = std::move(excluded)});
}

auto failure_mask_contains(const GenericFailureMask& mask, TypeID member) noexcept -> bool {
    return std::ranges::binary_search(mask.members, member) != mask.complemented;
}

GenericFailureAlgebra::GenericFailureAlgebra(ProgramIdentity owner) noexcept
    : expressions(owner) {}

auto GenericFailureAlgebra::owner() const noexcept -> ProgramIdentity {
    return expressions.owner();
}

auto GenericFailureAlgebra::contains(GenericFailureID id) const noexcept -> bool {
    return expressions.contains(id);
}

auto GenericFailureAlgebra::copy(GenericFailureID id) const noexcept -> GenericFailureExpression {
    return expressions.copy(id);
}

auto GenericFailureAlgebra::normalize_members(std::vector<TypeID> members) const noexcept
    -> std::vector<TypeID> {
    for (const auto member : members) {
        if (member.owner() != owner()) {
            invariant_violation("symbolic failure mask contains a foreign nominal type");
        }
    }
    std::ranges::sort(members);
    members.erase(std::ranges::unique(members).begin(), members.end());
    return members;
}

auto GenericFailureAlgebra::intern(GenericFailureExpression expression) noexcept
    -> GenericFailureID {
    expression.concrete_members = normalize_members(std::move(expression.concrete_members));
    auto normalized = GenericFailureExpression {
        .concrete_members = std::move(expression.concrete_members),
        .parameters = {},
        .guards = {},
    };
    for (auto& component : expression.parameters) {
        if (component.parameter.definition.owner() != owner()) {
            invariant_violation("symbolic failure parameter belongs to another semantic program");
        }
        component.mask.members = normalize_members(std::move(component.mask.members));
        merge_expression(
            normalized,
            {
                .concrete_members = {},
                .parameters = {std::move(component)},
                .guards = {},
            }
        );
    }
    auto pending = std::move(expression.guards);
    while (!pending.empty()) {
        const auto contribution = pending.back();
        pending.pop_back();
        if (!contains(contribution.gate) || !contains(contribution.source)) {
            invariant_violation("symbolic failure guard contains a foreign or invalid node");
        }
        if (empty(contribution.gate) == FailureProof::Proven
            || empty(contribution.source) == FailureProof::Proven) {
            continue;
        }
        if (non_empty(contribution.gate) == FailureProof::Proven
            || subset(contribution.source, contribution.gate) == FailureProof::Proven) {
            auto source = copy(contribution.source);
            pending.insert(pending.end(), source.guards.begin(), source.guards.end());
            source.guards.clear();
            merge_expression(normalized, std::move(source));
        } else {
            normalized.guards.push_back(contribution);
        }
    }
    for (auto& component : normalized.parameters) {
        component.mask = intersect_masks(
            component.mask,
            {
                .complemented = true,
                .members = normalized.concrete_members,
            }
        );
    }
    std::erase_if(normalized.parameters, [](const auto& component) static noexcept {
        return !component.mask.complemented && component.mask.members.empty();
    });
    std::ranges::sort(normalized.guards, {}, [](const auto& guard) static noexcept {
        return std::pair(guard.gate, guard.source);
    });
    normalized.guards.erase(
        std::ranges::unique(normalized.guards).begin(),
        normalized.guards.end()
    );
    return expressions.intern(std::move(normalized));
}

auto GenericFailureAlgebra::unite(std::span<const GenericFailureID> inputs) noexcept
    -> GenericFailureID {
    auto expression = GenericFailureExpression {};
    for (const auto input : inputs) {
        merge_expression(expression, copy(input));
    }
    return intern(std::move(expression));
}

auto GenericFailureAlgebra::filter(GenericFailureID input, GenericFailureMask mask) noexcept
    -> GenericFailureID {
    mask.members = normalize_members(std::move(mask.members));
    auto expression = copy(input);
    expression.concrete_members = combine_members(
        expression.concrete_members,
        mask.members,
        mask.complemented ? MemberOperation::Difference : MemberOperation::Intersection
    );
    for (auto& component : expression.parameters) {
        component.mask = intersect_masks(component.mask, mask);
    }
    for (auto& contribution : expression.guards) {
        // Filtering the source never changes whether the protected gate is inhabited.
        contribution.source = filter(contribution.source, mask);
    }
    return intern(std::move(expression));
}

auto GenericFailureAlgebra::residual(GenericFailureID input, std::vector<TypeID> members) noexcept
    -> GenericFailureID {
    return filter(input, {.complemented = true, .members = std::move(members)});
}

auto GenericFailureAlgebra::intersection(
    GenericFailureID input,
    std::vector<TypeID> members
) noexcept -> GenericFailureID {
    return filter(input, {.complemented = false, .members = std::move(members)});
}

auto GenericFailureAlgebra::guard(GenericFailureID gate, GenericFailureID source) noexcept
    -> GenericFailureID {
    // A single guarded value is inhabited exactly when both its gate and
    // payload are inhabited. Canonical conjunctions keep repeated substitution
    // from growing equivalent nested guards.
    auto factors = std::vector<GenericFailureID>();
    const auto collect = [&](auto&& self, GenericFailureID value) noexcept -> void {
        const auto expression = copy(value);
        if (expression.concrete_members.empty()
            && expression.parameters.empty()
            && expression.guards.size() == 1uz) {
            self(self, expression.guards[0].gate);
            self(self, expression.guards[0].source);
        } else {
            factors.push_back(value);
        }
    };
    collect(collect, gate);
    for (;;) {
        const auto expression = copy(source);
        if (!expression.concrete_members.empty()
            || !expression.parameters.empty()
            || expression.guards.size() != 1uz) {
            break;
        }
        collect(collect, expression.guards[0].gate);
        source = expression.guards[0].source;
    }
    std::ranges::sort(factors);
    factors.erase(std::ranges::unique(factors).begin(), factors.end());
    for (const auto factor : factors | std::views::reverse) {
        if (factor == source) {
            continue;
        }
        source = intern(
            {.concrete_members = {},
             .parameters = {},
             .guards = {{.gate = factor, .source = source}}}
        );
    }
    return source;
}

auto GenericFailureAlgebra::bounds(GenericFailureID source, bool upper) const noexcept
    -> GenericFailureExpression {
    auto memo = std::map<GenericFailureID, GenericFailureExpression>();
    const auto visit = [&](auto&& self, GenericFailureID id) noexcept -> GenericFailureExpression {
        if (const auto found = memo.find(id); found != memo.end()) {
            return found->second;
        }
        auto expression = copy(id);
        auto guards = std::exchange(expression.guards, {});
        if (upper) {
            for (const auto& contribution : guards) {
                merge_expression(expression, self(self, contribution.source));
            }
        }
        memo.emplace(id, expression);
        return expression;
    };
    return visit(visit, source);
}

auto GenericFailureAlgebra::subset(GenericFailureID actual, GenericFailureID allowed) const noexcept
    -> FailureProof {
    if (!contains(actual) || !contains(allowed)) {
        invariant_violation("symbolic failure proof used an invalid identity");
    }
    if (actual == allowed) {
        return FailureProof::Proven;
    }
    const auto actual_expression = copy(actual);
    const auto allowed_expression = copy(allowed);
    if (actual_expression.guards.empty() && allowed_expression.guards.empty()) {
        return normal_subset(actual_expression, allowed_expression) ? FailureProof::Proven
                                                                    : FailureProof::Disproven;
    }
    if (normal_subset(bounds(actual, true), bounds(allowed, false))) {
        return FailureProof::Proven;
    }
    if (!normal_subset(bounds(actual, false), bounds(allowed, true))) {
        return FailureProof::Disproven;
    }
    return FailureProof::Unknown;
}

auto GenericFailureAlgebra::empty(GenericFailureID source) const noexcept -> FailureProof {
    const auto upper = bounds(source, true);
    if (upper.concrete_members.empty() && upper.parameters.empty()) {
        return FailureProof::Proven;
    }
    if (!bounds(source, false).concrete_members.empty()) {
        return FailureProof::Disproven;
    }
    return FailureProof::Unknown;
}

auto GenericFailureAlgebra::non_empty(GenericFailureID source) const noexcept -> FailureProof {
    switch (empty(source)) {
        case FailureProof::Proven:    return FailureProof::Disproven;
        case FailureProof::Disproven: return FailureProof::Proven;
        case FailureProof::Unknown:   return FailureProof::Unknown;
    }
    std::unreachable();
}

auto GenericFailureAlgebra::support(GenericFailureID source) const noexcept -> GenericFailureMask {
    const auto expression = bounds(source, true);
    auto mask = GenericFailureMask {.complemented = false, .members = expression.concrete_members};
    for (const auto& component : expression.parameters) {
        mask = union_masks(mask, component.mask);
    }
    return mask;
}

auto GenericFailureAlgebra::known_members(GenericFailureID source) const noexcept
    -> std::vector<TypeID> {
    return bounds(source, false).concrete_members;
}

auto GenericFailureAlgebra::lower_bound(GenericFailureID source) noexcept -> GenericFailureID {
    return intern(bounds(source, false));
}

auto GenericFailureAlgebra::upper_bound(GenericFailureID source) noexcept -> GenericFailureID {
    return intern(bounds(source, true));
}

auto GenericFailureAlgebra::ids() const noexcept -> std::vector<GenericFailureID> {
    const auto snapshot = expressions.snapshot();
    auto result = std::vector<GenericFailureID>();
    result.reserve(snapshot.size());
    for (const auto [id, value] : snapshot.entries()) {
        static_cast<void>(value);
        result.push_back(id);
    }
    return result;
}
