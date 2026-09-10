module carven:test.internal.backend.construction.fixture;

import :backend.construction;
import :semantic.semir;
import std;

// Malformed construction is confined to tests; production only publishes a
// checked immutable body through BodyConstructionBuilder::finish.
class ConstructionTestingFixture final {
public:
    static auto expression_id(BodyID body, std::uint32_t index) noexcept
        -> ConstructionExpressionID {
        return ConstructionExpressionID(body, index);
    }

    static auto region_id(BodyID body, std::uint32_t index) noexcept -> ConstructionRegionID {
        return ConstructionRegionID(body, index);
    }

    static auto expressions(BodyConstruction& body) noexcept -> std::span<ConstructionExpression> {
        return body.expressions;
    }

    static auto regions(BodyConstruction& body) noexcept -> std::span<ConstructionRegion> {
        return body.regions;
    }
};
