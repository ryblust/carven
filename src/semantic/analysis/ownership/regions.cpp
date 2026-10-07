module carven:semantic.analysis.ownership.regions.impl;

import :semantic.analysis.ownership.context;
import std;

OwnershipRegionIndex::OwnershipRegionIndex(
    std::span<const OwnershipStorageEdge> edges,
    std::size_t objects,
    std::span<const std::pair<std::size_t, std::size_t>> possible_aliases
) noexcept
    : transitions(objects) {
    auto prefixes = std::map<std::pair<std::size_t, std::optional<std::uint64_t>>, std::size_t>();
    for (const auto& edge : edges) {
        auto word = edge.carrier.path;
        word.push_back(edge.index);
        auto current = edge.carrier.object;
        for (const auto [index, label] : std::views::enumerate(word)) {
            if (index + 1uz == word.size()) {
                transitions[current].push_back({label, edge.element});
            } else {
                const auto [found, inserted] =
                    prefixes.try_emplace({current, label}, transitions.size());
                if (inserted) {
                    transitions.emplace_back();
                    transitions[current].push_back({label, found->second});
                }
                current = found->second;
            }
        }
    }
    product.resize(transitions.size());
    incoming.resize(transitions.size());
    auto pending = std::vector<std::pair<std::size_t, std::size_t>>();
    const auto enqueue = [&](std::size_t left, std::size_t right) noexcept {
        auto& row = product[left][right / 64uz];
        const auto bit = 1ull << (right % 64uz);
        if ((row & bit) == 0ull) {
            row |= bit;
            pending.emplace_back(left, right);
        }
    };
    for (auto object = 0uz; object < objects; ++object) {
        enqueue(object, object);
    }
    // Equal-region possibilities are seed pairs, never equivalence classes.
    for (const auto [left, right] : possible_aliases) {
        enqueue(left, right);
        enqueue(right, left);
    }
    for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
        const auto [left, right] = pending[cursor];
        for (const auto& a : transitions[left]) {
            for (const auto& b : transitions[right]) {
                if (!a.label || !b.label || a.label == b.label) {
                    enqueue(a.target, b.target);
                }
            }
        }
    }
    for (const auto [index, outgoing] : std::views::enumerate(transitions)) {
        for (const auto& edge : outgoing) {
            incoming[edge.target].push_back(static_cast<std::size_t>(index));
        }
    }
}

auto OwnershipRegionIndex::equal_paths(std::size_t left, std::size_t right) const noexcept -> bool {
    const auto found = product[left].find(right / 64uz);
    return found != product[left].end() && (found->second & (1ull << (right % 64uz))) != 0ull;
}

auto OwnershipRegionIndex::reaches(std::size_t object) const noexcept -> const std::vector<bool>& {
    const auto [found, inserted] = reach_cache.try_emplace(object);
    if (inserted) {
        auto& reached = found->second;
        reached.resize(transitions.size(), false);
        reached[object] = true;
        auto pending = std::vector<std::size_t> {object};
        for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
            for (const auto previous : incoming[pending[cursor]]) {
                if (!reached[previous]) {
                    reached[previous] = true;
                    pending.push_back(previous);
                }
            }
        }
    }
    return found->second;
}

auto OwnershipRegionIndex::matches(
    const OwnershipPlace& left,
    const OwnershipPlace& right,
    OwnershipRegionRelation relation
) const noexcept -> bool {
    const auto key = std::tuple(left, right, relation);
    const auto [found, inserted] = results.try_emplace(key);
    if (inserted) {
        found->second = query(left, right, relation);
    }
    return found->second;
}

auto OwnershipRegionIndex::query(
    const OwnershipPlace& left,
    const OwnershipPlace& right,
    OwnershipRegionRelation relation
) const noexcept -> bool {
    const auto equal = relation == OwnershipRegionRelation::Alias;
    const auto ancestor = relation == OwnershipRegionRelation::Ancestor
        || relation == OwnershipRegionRelation::StrictAncestor;
    const auto strict = relation == OwnershipRegionRelation::StrictAncestor;
    // Equal terminal lengths require the base words to end at the same length.
    // Wildcard compatibility remains a possible relation, not object identity.
    if (equal && left.path.size() == right.path.size()) {
        return overlaps(left.path, right.path) && equal_paths(left.object, right.object);
    }
    const auto size = transitions.size();
    const auto& left_reach = reaches(left.object);
    const auto& right_reach = reaches(right.object);
    auto pending = std::vector<std::pair<std::size_t, std::size_t>>();
    auto visited =
        std::vector<std::flat_map<std::size_t, std::uint64_t>>(size + left.path.size() + 1uz);
    const auto enqueue = [&](std::size_t a, std::size_t b) noexcept {
        auto& row = visited[a][b / 64uz];
        const auto bit = 1ull << (b % 64uz);
        if ((row & bit) == 0ull) {
            row |= bit;
            pending.emplace_back(a, b);
        }
    };
    // At least one side has entered its terminal chain. Every earlier pair of
    // base states has already been explored by the topology's shared closure.
    for (auto state = 0uz; state < size; ++state) {
        if (equal_paths(left.object, state) && right_reach[state]) {
            enqueue(size, state);
        }
        if (equal_paths(state, right.object) && left_reach[state]) {
            enqueue(state, size);
        }
    }
    const auto accepts = [&](std::size_t state, const OwnershipPlace& place) noexcept {
        return state == size + place.path.size();
    };
    const auto can_finish = [&](std::size_t state, const std::vector<bool>& reach) noexcept {
        return state >= size || reach[state];
    };
    const auto nonempty_finish = [&](std::size_t state,
                                     const OwnershipPlace& place,
                                     const std::vector<bool>& reach) noexcept {
        if (state >= size) {
            return state < size + place.path.size();
        }
        return (!place.path.empty() && reach[state])
            || std::ranges::any_of(transitions[state], [&](const auto& edge) noexcept {
                   return reach[edge.target];
               });
    };
    const auto visit_transitions =
        [&](std::size_t state, const OwnershipPlace& place, const auto& visit) noexcept {
            if (state < size) {
                for (const auto& edge : transitions[state]) {
                    visit(edge);
                }
            } else if (const auto position = state - size; position < place.path.size()) {
                visit(Transition {place.path[position], state + 1uz});
            }
        };
    for (auto cursor = 0uz; cursor < pending.size(); ++cursor) {
        const auto [a, b] = pending[cursor];
        if (equal && accepts(a, left) && accepts(b, right)) {
            return true;
        }
        if (!equal
            && accepts(a, left)
            && can_finish(b, right_reach)
            && (!ancestor || !strict || nonempty_finish(b, right, right_reach))) {
            return true;
        }
        if (!equal && !ancestor && accepts(b, right) && can_finish(a, left_reach)) {
            return true;
        }
        // Entering a terminal suffix is an epsilon choice, independent of the
        // other side's next symbol. Once chosen, it never returns to the base.
        if (a < size && a == left.object) {
            enqueue(size, b);
        }
        if (b < size && b == right.object) {
            enqueue(a, size);
        }
        visit_transitions(a, left, [&](const auto& x) noexcept {
            if (!can_finish(x.target, left_reach)) {
                return;
            }
            visit_transitions(b, right, [&](const auto& y) noexcept {
                if (can_finish(y.target, right_reach)
                    && (!x.label || !y.label || x.label == y.label)) {
                    enqueue(x.target, y.target);
                }
            });
        });
    }
    return false;
}
