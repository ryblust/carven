module carven:backend.construction.impl;

import :backend.construction;
import :semantic.semir;
import :support.invariant;
import std;

auto BodyConstruction::body() const noexcept -> BodyID {
    return body_id;
}

auto BodyConstruction::root() const noexcept -> ConstructionRegionID {
    return root_region;
}

auto BodyConstruction::expression(ConstructionExpressionID id) const noexcept
    -> const ConstructionExpression& {
    if (id.owner() != body_id || id.index() >= expressions.size()) {
        invariant_violation("construction expression belongs to another body or is out of range");
    }
    return expressions[id.index()];
}

auto BodyConstruction::region(ConstructionRegionID id) const noexcept -> const ConstructionRegion& {
    if (id.owner() != body_id || id.index() >= regions.size()) {
        invariant_violation("construction region belongs to another body or is out of range");
    }
    return regions[id.index()];
}

auto BodyConstruction::expression_values() const noexcept
    -> std::span<const ConstructionExpression> {
    return expressions;
}

auto BodyConstruction::region_values() const noexcept -> std::span<const ConstructionRegion> {
    return regions;
}

BodyConstruction::BodyConstruction(
    BodyID body,
    ConstructionRegionID root,
    std::vector<ConstructionExpression> expressions,
    std::vector<ConstructionRegion> regions
) noexcept
    : body_id(body),
      root_region(root),
      expressions(std::move(expressions)),
      regions(std::move(regions)) {}
