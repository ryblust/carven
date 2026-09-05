module carven:semantic.analysis.body.scopes.impl;

import :semantic.analysis.body.builder;
import :support.invariant;
import std;

auto BodyBuilder::add_scope(std::optional<ScopeID> parent, ProgramOriginID origin) noexcept
    -> ScopeID {
    if (parent.has_value()) {
        if (!scopes.contains(*parent)) {
            invariant_violation("foreign scope parent");
        }
    }
    return scopes.add(Scope {.parent = parent, .origin = origin});
}

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
