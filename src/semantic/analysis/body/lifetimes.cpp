module carven:semantic.analysis.body.lifetimes.impl;

import :semantic.analysis.body.builder;
import :support.invariant;
import std;

auto BodyBuilder::add_lifetime_region(
    std::optional<LifetimeRegionID> parent,
    LifetimeRegionKind kind,
    ProgramOriginID origin
) noexcept -> LifetimeRegionID {
    if (parent.has_value()) {
        if (!lifetime_regions.contains(*parent)) {
            invariant_violation("foreign lifetime parent");
        }
    }
    return lifetime_regions.add(
        LifetimeRegion {
            .parent = parent,
            .kind = kind,
            .origin = origin,
        }
    );
}
