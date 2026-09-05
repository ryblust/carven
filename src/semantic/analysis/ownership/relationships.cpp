module carven:semantic.analysis.ownership.relationships.impl;

import :semantic.analysis.ownership.context;
import std;

namespace ownership {
namespace {

template<typename T>
auto merge_rows(std::vector<T>& destination, const std::vector<T>& source) noexcept -> void {
    for (const auto& item : source) {
        if (!std::ranges::contains(destination, item)) {
            destination.push_back(item);
        }
    }
    std::ranges::sort(destination);
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
auto overlaps(const Place& left, const Place& right) noexcept -> bool {
    return left.object == right.object && overlaps(left.path, right.path);
}
auto merge_relationships(Relationships& destination, const Relationships& source) noexcept -> void {
    merge_rows(destination.loans, source.loans);
    merge_rows(destination.captures, source.captures);
}
auto project(const Relationships& source, const ProjectionPath& path) noexcept -> Relationships {
    auto result = Relationships {};
    const auto select = [&](const auto& rows, auto& destination) noexcept {
        for (auto row : rows) {
            if (row.holder.size() < path.size() || !overlaps(row.holder, path)) {
                continue;
            }
            row.holder.erase(
                row.holder.begin(),
                row.holder.begin() + static_cast<std::ptrdiff_t>(path.size())
            );
            destination.push_back(std::move(row));
        }
    };
    select(source.loans, result.loans);
    select(source.captures, result.captures);
    return result;
}
auto nested(Relationships source, const ProjectionPath& path) noexcept -> Relationships {
    for (auto& loan : source.loans) {
        loan.holder.insert(loan.holder.begin(), path.begin(), path.end());
    }
    for (auto& capture : source.captures) {
        capture.holder.insert(capture.holder.begin(), path.begin(), path.end());
    }
    return source;
}
auto join(State& destination, const State& source) noexcept -> void {
    if (destination.objects.size() != source.objects.size()) {
        invariant_violation("ownership join has different storage domains");
    }
    for (auto&& [target, incoming] : std::views::zip(destination.objects, source.objects)) {
        target.available &= incoming.available;
        if (incoming.taken.has_value()
            && (!target.taken.has_value() || *incoming.taken < *target.taken)) {
            target.taken = incoming.taken;
        }
        merge_relationships(target.relationships, incoming.relationships);
    }
}
auto join_normal(std::optional<State>& destination, const std::optional<State>& source) noexcept
    -> void {
    if (!source.has_value()) {
        return;
    }
    if (!destination.has_value()) {
        destination = source;
    } else {
        join(*destination, *source);
    }
}
auto append_exits(Flow& destination, Flow& source) noexcept -> void {
    for (auto& exit : source.exits) {
        const auto found = std::ranges::find_if(destination.exits, [&](const Exit& other) noexcept {
            return exit.kind == other.kind && exit.failure == other.failure;
        });
        if (found == destination.exits.end()) {
            destination.exits.push_back(std::move(exit));
        } else {
            join(found->state, exit.state);
            merge_relationships(found->value, exit.value);
        }
    }
}

} // namespace ownership
