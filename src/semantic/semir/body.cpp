module carven:semantic.semir.body.impl;

import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.identity;
import :semantic.semir.ids;
import :semantic.semir.table;
import :semantic.semir.type;
import :source.provenance.ids;
import :support.invariant;
import :support.visit;
import std;

LifetimeRegionTree::LifetimeRegionTree(
    ImmutableBodyTable<LifetimeRegion, LifetimeRegionID> rows
) noexcept
    : region_rows(std::move(rows)) {}

auto LifetimeRegionTree::owner() const noexcept -> BodyIdentity {
    return region_rows.owner();
}

auto LifetimeRegionTree::contains(LifetimeRegionID id) const noexcept -> bool {
    return region_rows.contains(id);
}

auto LifetimeRegionTree::region(LifetimeRegionID id) const noexcept -> const LifetimeRegion& {
    return region_rows.get(id);
}

auto LifetimeRegionTree::outlives(LifetimeRegionID outer, LifetimeRegionID inner) const noexcept
    -> bool {
    if (!contains(outer) || !contains(inner)) {
        invariant_violation("lifetime relation received a foreign region");
    }
    auto current = std::optional {inner};
    while (current.has_value()) {
        if (*current == outer) {
            return true;
        }
        current = region(*current).parent;
    }
    return false;
}

auto LifetimeRegionTree::entries() const noexcept
    -> IDTableEntries<LifetimeRegionID, LifetimeRegion, BodyIdentity> {
    return region_rows.entries();
}
