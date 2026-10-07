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

auto select_element_storage(
    const CanonicalTypeStore& types,
    TypeID sequence,
    std::span<const OwnershipPlace> storage,
    const OwnershipRelationships& relationships,
    std::optional<std::uint64_t> index
) noexcept -> std::vector<OwnershipPlace> {
    auto result = std::vector<OwnershipPlace>();
    if (std::holds_alternative<SliceTypeValue>(types.type(sequence).value)) {
        for (const auto& loan : relationships.view().storage_loans) {
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
    if (destination.objects.size() != source.objects.size()) {
        invariant_violation("ownership join has different storage domains");
    }
    for (auto&& [target, incoming] : std::views::zip(destination.objects, source.objects)) {
        target.available &= incoming.available;
        target.modified |= incoming.modified;
        if (incoming.child_intent) {
            target.child_intent = target.child_intent
                ? std::optional(*target.child_intent && *incoming.child_intent)
                : incoming.child_intent;
        }
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
