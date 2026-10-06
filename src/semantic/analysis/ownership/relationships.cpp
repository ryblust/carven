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

auto storage_region_automaton(
    std::span<const OwnershipStorageEdge> edges,
    const OwnershipPlace& source
) noexcept -> OwnershipRegionAutomaton {
    auto objects = source.object + 1uz;
    for (const auto& edge : edges) {
        objects = std::max(objects, std::max(edge.carrier.object, edge.element) + 1uz);
    }
    auto result = OwnershipRegionAutomaton {objects, source.object, {}, {}};
    result.transitions.resize(objects);
    const auto append = [&](std::size_t from,
                            const OwnershipProjectionPath& word,
                            std::optional<std::size_t> destination) noexcept {
        auto current = from;
        for (const auto [index, label] : std::views::enumerate(word)) {
            auto next = result.transitions.size();
            if (destination && index + 1uz == word.size()) {
                next = *destination;
            } else {
                result.transitions.emplace_back();
            }
            result.transitions[current].push_back({label, next});
            current = next;
        }
        return current;
    };
    for (const auto& edge : edges) {
        auto word = edge.carrier.path;
        word.push_back(edge.index);
        static_cast<void>(append(edge.carrier.object, word, edge.element));
    }
    result.accept = append(source.object, source.path, std::nullopt);
    result.reaches_accept.resize(result.transitions.size(), false);
    result.reaches_accept[result.accept] = true;
    auto incoming = std::vector<std::vector<std::size_t>>(result.transitions.size());
    for (const auto [index, outgoing] : std::views::enumerate(result.transitions)) {
        for (const auto& edge : outgoing) {
            incoming[edge.target].push_back(static_cast<std::size_t>(index));
        }
    }
    auto pending = std::vector<std::size_t> {result.accept};
    for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
        for (const auto previous : incoming[pending[cursor]]) {
            if (!result.reaches_accept[previous]) {
                result.reaches_accept[previous] = true;
                pending.push_back(previous);
            }
        }
    }
    return result;
}

auto storage_region_matches(
    const OwnershipRegionAutomaton& left,
    const OwnershipRegionAutomaton& right,
    bool ancestor,
    bool strict,
    bool equal
) noexcept -> bool {
    using Pair = std::pair<std::size_t, std::size_t>;
    auto pending = std::vector<Pair>();
    auto visited = std::set<Pair>();
    const auto enqueue = [&](std::size_t a, std::size_t b) noexcept {
        if (visited.emplace(a, b).second) {
            pending.emplace_back(a, b);
        }
    };
    for (auto root = 0uz; root < std::min(left.objects, right.objects); ++root) {
        if (left.reaches_accept[root] && right.reaches_accept[root]) {
            enqueue(root, root);
        }
    }
    for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
        const auto [a, b] = pending[cursor];
        if (equal && a == left.accept && b == right.accept) {
            return true;
        }
        if (!equal && a == left.accept && right.reaches_accept[b]) {
            if (!ancestor
                || !strict
                || std::ranges::any_of(right.transitions[b], [&](const auto& edge) noexcept {
                       return right.reaches_accept[edge.target];
                   })) {
                return true;
            }
        }
        if (!equal && !ancestor && b == right.accept && left.reaches_accept[a]) {
            return true;
        }
        for (const auto& x : left.transitions[a]) {
            if (!left.reaches_accept[x.target]) {
                continue;
            }
            for (const auto& y : right.transitions[b]) {
                if (right.reaches_accept[y.target]
                    && (!x.label || !y.label || x.label == y.label)) {
                    enqueue(x.target, y.target);
                }
            }
        }
    }
    return false;
}

auto storage_region_ancestor(
    std::span<const OwnershipStorageEdge> edges,
    const OwnershipPlace& owner,
    const OwnershipPlace& referent,
    bool strict
) noexcept -> bool {
    return storage_region_matches(
        storage_region_automaton(edges, owner),
        storage_region_automaton(edges, referent),
        true,
        strict
    );
}

auto storage_regions_overlap(
    std::span<const OwnershipStorageEdge> edges,
    const OwnershipPlace& left,
    const OwnershipPlace& right
) noexcept -> bool {
    return storage_region_matches(
        storage_region_automaton(edges, left),
        storage_region_automaton(edges, right),
        false,
        false
    );
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
