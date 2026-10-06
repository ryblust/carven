module carven:semantic.analysis.ownership.relationships.impl;

import :semantic.analysis.ownership.context;
import std;

namespace {

template<typename T>
auto normalize_rows(std::vector<T>& rows) noexcept -> void {
    if (rows.size() < 2uz) {
        return;
    }
    std::ranges::sort(rows, [](const T& left, const T& right) static noexcept {
        const auto order = left <=> right;
        return order < 0 || (order == 0 && left.origin < right.origin);
    });
    rows.erase(std::ranges::unique(rows).begin(), rows.end());
}

template<typename T>
auto merge_rows(std::vector<T>& destination, const std::vector<T>& source) noexcept -> void {
    if (!source.empty() && std::addressof(destination) != std::addressof(source)) {
        destination.insert(destination.end(), source.begin(), source.end());
    }
    normalize_rows(destination);
}

} // namespace

auto overlaps(
    std::span<const std::optional<std::uint64_t>> left,
    std::span<const std::optional<std::uint64_t>> right
) noexcept -> bool {
    for (const auto [a, b] : std::views::zip(left, right)) {
        if (a.has_value() && b.has_value() && a != b) {
            return false;
        }
    }
    return true;
}

auto overlaps(const OwnershipPlace& left, const OwnershipPlace& right) noexcept -> bool {
    return left.object == right.object && overlaps(left.path, right.path);
}

auto project_storage_region(
    std::span<const OwnershipStorageEdge> edges,
    const OwnershipPlace& source
) noexcept -> std::vector<OwnershipRegionProjection> {
    struct Frame final {
        OwnershipRegionProjection region;
        std::vector<std::size_t> ancestors;
    };

    auto result = std::vector<OwnershipRegionProjection>();
    auto pending = std::vector<Frame> {{{source, true}, {source.object}}};
    auto visited = std::flat_set<std::pair<OwnershipPlace, bool>>();
    for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
        const auto current = pending[cursor];
        if (!visited.emplace(current.region.place, current.region.exact).second) {
            continue;
        }
        result.push_back(current.region);
        for (const auto& edge : edges) {
            if (edge.element != current.region.place.object) {
                continue;
            }
            auto projected = edge.carrier;
            projected.path.push_back(edge.index);
            const auto exact = current.region.exact
                && !std::ranges::contains(current.ancestors, edge.carrier.object);
            if (exact) {
                projected.path.append_range(current.region.place.path);
            }
            auto ancestors = current.ancestors;
            if (exact) {
                ancestors.push_back(edge.carrier.object);
            } else {
                // Recursive paths widen to a finite edge region. They still
                // propagate to external ancestors instead of stopping at a cycle.
                ancestors.clear();
            }
            pending.push_back({{std::move(projected), exact}, std::move(ancestors)});
        }
    }
    return result;
}

auto storage_region_ancestor(
    std::span<const OwnershipStorageEdge> edges,
    const OwnershipPlace& owner,
    const OwnershipPlace& referent,
    bool strict
) noexcept -> bool {
    return storage_region_ancestor(
        owner,
        project_storage_region(edges, owner),
        project_storage_region(edges, referent),
        strict
    );
}

auto storage_region_ancestor(
    const OwnershipPlace& owner,
    std::span<const OwnershipRegionProjection> owners,
    std::span<const OwnershipRegionProjection> referents,
    bool strict
) noexcept -> bool {
    for (const auto& a : owners) {
        for (const auto& b : referents) {
            if (a.exact
                && b.exact
                && overlaps(a.place, b.place)
                && (strict ? a.place.path.size() < b.place.path.size()
                           : a.place.path.size() <= b.place.path.size())) {
                return true;
            }
        }
    }
    // A cycle region can protect descendants from an actual ancestor write.
    // Two independently widened regions do not establish containment.
    return std::ranges::any_of(referents, [&](const auto& region) noexcept {
        return !region.exact && overlaps(owner, region.place);
    });
}

auto storage_regions_overlap(
    std::span<const OwnershipStorageEdge> edges,
    const OwnershipPlace& left,
    const OwnershipPlace& right
) noexcept -> bool {
    return storage_regions_overlap(
        left,
        right,
        project_storage_region(edges, left),
        project_storage_region(edges, right)
    );
}

auto storage_regions_overlap(
    const OwnershipPlace& left,
    const OwnershipPlace& right,
    std::span<const OwnershipRegionProjection> left_regions,
    std::span<const OwnershipRegionProjection> right_regions
) noexcept -> bool {
    for (const auto& a : left_regions) {
        for (const auto& b : right_regions) {
            if (a.exact && b.exact && overlaps(a.place, b.place)) {
                return true;
            }
        }
    }
    // Widening bounds one referent; a shared bound of two referents does not
    // erase their original field separation.
    return std::ranges::any_of(
               left_regions,
               [&](const auto& region) noexcept {
                   return !region.exact && overlaps(region.place, right);
               }
           )
        || std::ranges::any_of(right_regions, [&](const auto& region) noexcept {
               return !region.exact && overlaps(left, region.place);
           });
}

auto normalize_storage_loans(std::vector<OwnershipStorageLoan>& loans) noexcept -> void {
    normalize_rows(loans);
}

auto normalize_relationships(OwnershipRelationships& relationships) noexcept -> void {
    if (auto* rows = relationships.edit_existing()) {
        normalize_rows(rows->callable_loans);
        normalize_rows(rows->captures);
        normalize_storage_loans(rows->storage_loans);
    }
}

auto merge_relationships(
    OwnershipRelationships& destination,
    const OwnershipRelationships& source
) noexcept -> void {
    if (source.empty()) {
        // Even an empty input canonicalizes facts already held by destination.
        normalize_relationships(destination);
        return;
    }
    auto& rows = destination.edit();
    const auto& incoming = source.view();
    merge_rows(rows.callable_loans, incoming.callable_loans);
    merge_rows(rows.captures, incoming.captures);
    merge_rows(rows.storage_loans, incoming.storage_loans);
}

auto project_relationships(
    const OwnershipRelationships& source,
    const OwnershipProjectionPath& path
) noexcept -> OwnershipRelationships {
    auto result = OwnershipRelationships {};
    const auto select = [&](const auto& rows, auto member) noexcept {
        for (auto row : rows) {
            if (row.holder.size() < path.size() || !overlaps(row.holder, path)) {
                continue;
            }
            row.holder.erase(
                row.holder.begin(),
                row.holder.begin() + static_cast<std::ptrdiff_t>(path.size())
            );
            (result.edit().*member).push_back(std::move(row));
        }
    };
    const auto& rows = source.view();
    select(rows.callable_loans, &OwnershipRelationshipRows::callable_loans);
    select(rows.captures, &OwnershipRelationshipRows::captures);
    select(rows.storage_loans, &OwnershipRelationshipRows::storage_loans);
    normalize_relationships(result);
    return result;
}

auto nest_relationships(OwnershipRelationships source, const OwnershipProjectionPath& path) noexcept
    -> OwnershipRelationships {
    if (auto* rows = source.edit_existing()) {
        for (auto& loan : rows->callable_loans) {
            loan.holder.insert(loan.holder.begin(), path.begin(), path.end());
        }
        for (auto& loan : rows->storage_loans) {
            loan.holder.insert(loan.holder.begin(), path.begin(), path.end());
        }
        for (auto& capture : rows->captures) {
            capture.holder.insert(capture.holder.begin(), path.begin(), path.end());
        }
    }
    return source;
}

auto join_ownership_state(OwnershipState& destination, const OwnershipState& source) noexcept
    -> void {
    // Query-local referents append to a stable domain. A branch that did not
    // select a referent has no relationships for it, rather than unavailable storage.
    if (destination.objects.size() < source.objects.size()) {
        destination.objects.resize(
            source.objects.size(),
            {.available = true, .taken = std::nullopt, .relationships = {}, .modified = false}
        );
    }
    for (auto&& [target, incoming] : std::views::zip(destination.objects, source.objects)) {
        target.available &= incoming.available;
        target.modified |= incoming.modified;
        if (incoming.taken.has_value()
            && (!target.taken.has_value() || *incoming.taken < *target.taken)) {
            target.taken = incoming.taken;
        }
        merge_relationships(target.relationships, incoming.relationships);
    }
}

auto join_normal_ownership(
    std::optional<OwnershipNormal>& destination,
    const std::optional<OwnershipNormal>& source
) noexcept -> void {
    if (!source.has_value()) {
        return;
    }
    if (!destination.has_value()) {
        destination = source;
    } else {
        join_ownership_state(destination->state, source->state);
        merge_relationships(destination->value, source->value);
    }
}

auto join_normal_ownership(
    std::optional<OwnershipNormal>& destination,
    std::optional<OwnershipNormal>&& source
) noexcept -> void {
    if (!destination.has_value()) {
        destination = std::move(source);
    } else {
        join_normal_ownership(destination, source);
    }
}

auto append_ownership_exits(OwnershipFlow& destination, OwnershipFlow& source) noexcept -> void {
    for (auto& exit : source.exits) {
        const auto found =
            std::ranges::find_if(destination.exits, [&](const OwnershipExit& other) noexcept {
                if (exit.payload.index() != other.payload.index()) {
                    return false;
                }
                const auto* failure = std::get_if<OwnershipFailure>(&exit.payload);
                return !failure || failure->type == std::get<OwnershipFailure>(other.payload).type;
            });
        if (found == destination.exits.end()) {
            destination.exits.push_back(std::move(exit));
        } else {
            join_ownership_state(found->state, exit.state);
            if (auto* returned = std::get_if<OwnershipReturn>(&found->payload)) {
                merge_relationships(returned->value, std::get<OwnershipReturn>(exit.payload).value);
            } else if (auto* failure = std::get_if<OwnershipFailure>(&found->payload)) {
                merge_relationships(failure->value, std::get<OwnershipFailure>(exit.payload).value);
            }
        }
    }
}
