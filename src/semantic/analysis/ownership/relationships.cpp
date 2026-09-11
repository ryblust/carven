module carven:semantic.analysis.ownership.relationships.impl;

import :semantic.analysis.ownership.context;
import std;

namespace {

template<typename T>
auto normalize_rows(std::vector<T>& rows) noexcept -> void {
    std::ranges::sort(rows, [](const T& left, const T& right) static noexcept {
        const auto order = left <=> right;
        return order < 0 || (order == 0 && left.origin < right.origin);
    });
    rows.erase(std::ranges::unique(rows).begin(), rows.end());
}

template<typename T>
auto merge_rows(std::vector<T>& destination, const std::vector<T>& source) noexcept -> void {
    if (std::addressof(destination) != std::addressof(source)) {
        destination.insert(destination.end(), source.begin(), source.end());
    }
    normalize_rows(destination);
}

} // namespace

auto select_element_storage(
    const CanonicalTypeStore& types,
    TypeID sequence,
    std::span<const OwnershipPlace> storage,
    const OwnershipRelationships& relationships,
    std::optional<std::uint64_t> index
) noexcept -> std::vector<OwnershipPlace> {
    auto result = std::vector<OwnershipPlace>();
    if (std::holds_alternative<SliceTypeValue>(types.type(sequence).value)) {
        for (const auto& loan : relationships.storage_loans) {
            if (loan.holder.empty()) {
                result.push_back(loan.backing);
            }
        }
        // A slice can rebase indices; its backing is protected as a whole.
        index = std::nullopt;
    } else {
        result.assign(storage.begin(), storage.end());
    }
    for (auto& selected : result) {
        selected.path.push_back(index);
    }
    std::ranges::sort(result);
    result.erase(std::ranges::unique(result).begin(), result.end());
    return result;
}

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

auto normalize_storage_loans(std::vector<OwnershipStorageLoan>& loans) noexcept -> void {
    normalize_rows(loans);
}

auto normalize_relationships(OwnershipRelationships& relationships) noexcept -> void {
    normalize_rows(relationships.callable_loans);
    normalize_rows(relationships.captures);
    normalize_storage_loans(relationships.storage_loans);
}

auto merge_relationships(
    OwnershipRelationships& destination,
    const OwnershipRelationships& source
) noexcept -> void {
    merge_rows(destination.callable_loans, source.callable_loans);
    merge_rows(destination.captures, source.captures);
    merge_rows(destination.storage_loans, source.storage_loans);
}

auto project_relationships(
    const OwnershipRelationships& source,
    const OwnershipProjectionPath& path
) noexcept -> OwnershipRelationships {
    auto result = OwnershipRelationships {};
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
    select(source.callable_loans, result.callable_loans);
    select(source.captures, result.captures);
    select(source.storage_loans, result.storage_loans);
    normalize_relationships(result);
    return result;
}

auto nest_relationships(OwnershipRelationships source, const OwnershipProjectionPath& path) noexcept
    -> OwnershipRelationships {
    for (auto& loan : source.callable_loans) {
        loan.holder.insert(loan.holder.begin(), path.begin(), path.end());
    }
    for (auto& loan : source.storage_loans) {
        loan.holder.insert(loan.holder.begin(), path.begin(), path.end());
    }
    for (auto& capture : source.captures) {
        capture.holder.insert(capture.holder.begin(), path.begin(), path.end());
    }
    return source;
}

auto join_ownership_state(OwnershipState& destination, const OwnershipState& source) noexcept
    -> void {
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
