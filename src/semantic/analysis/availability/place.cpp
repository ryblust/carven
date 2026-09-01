module carven:semantic.analysis.availability.place.impl;

import :semantic.analysis.availability.place;
import :semantic.analysis.session.read;
import :semantic.hir.ids;
import :support.invariant;
import std;

AvailabilityPlaceCatalog::AvailabilityPlaceCatalog(SemanticDraftView hir) noexcept
    : place_refs(hir.symbol_count()),
      body_places(hir.bodies().size()) {
    auto roots = std::vector<std::optional<BodyID>>(hir.scopes().size());
    for (auto index = 0uz; index < hir.bodies().size(); ++index) {
        const auto body = BodyID::from_index(static_cast<std::uint32_t>(index));
        const auto scope = hir.body(body).scope;
        if (scope.index() >= roots.size() || roots[scope.index()].has_value()) {
            invariant_violation("validated semantic bodies do not have unique root scopes");
        }
        roots[scope.index()] = body;
    }

    auto scope_bodies = std::vector<std::optional<BodyID>>(hir.scopes().size());
    for (auto index = 0uz; index < hir.scopes().size(); ++index) {
        const auto& scope = hir.scopes()[index];
        if (scope.parent.has_value()) {
            if (scope.parent->index() >= index) {
                invariant_violation("validated semantic scopes are not construction-ordered");
            }
            scope_bodies[index] = scope_bodies[scope.parent->index()];
        }
        if (roots[index].has_value()) {
            scope_bodies[index] = roots[index];
        }
    }

    for (auto index = 0uz; index < hir.symbol_count(); ++index) {
        const auto symbol = SymbolID::from_index(static_cast<std::uint32_t>(index));
        const auto& binding = hir.binding(symbol);
        if (!binding.has_value()) {
            continue;
        }
        const auto scope = binding->scope;
        if (scope.index() >= scope_bodies.size()) {
            invariant_violation("validated semantic binding has an unknown scope");
        }
        const auto body = scope_bodies[scope.index()];
        if (!body.has_value()) {
            continue;
        }
        auto& places = body_places[body->index()];
        const auto local = AvailabilityPlaceID {
            .value = static_cast<std::uint32_t>(places.size()),
        };
        places.push_back(symbol);
        place_refs[index] = AvailabilityPlaceRef {.body = *body, .local = local};
    }
}
auto AvailabilityPlaceCatalog::local(BodyID body, SymbolID symbol) const noexcept
    -> AvailabilityPlaceID {
    if (symbol.index() >= place_refs.size()
        || !place_refs[symbol.index()].has_value()
        || place_refs[symbol.index()]->body != body) {
        invariant_violation("availability references a place owned by another body");
    }
    return place_refs[symbol.index()]->local;
}

auto AvailabilityPlaceCatalog::global(BodyID body, AvailabilityPlaceID place) const noexcept
    -> SymbolID {
    const auto& places = body_places[body.index()];
    if (place.value >= places.size()) {
        invariant_violation("availability references an unknown body-local place");
    }
    return places[place.value];
}

auto AvailabilityPlaceCatalog::count(BodyID body) const noexcept -> std::size_t {
    return body_places[body.index()].size();
}
