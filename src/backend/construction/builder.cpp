module carven:backend.construction.builder.impl;

import :backend.construction;
import :backend.construction.builder;
import :backend.construction.verify;
import :semantic.semir;
import :support.invariant;
import std;

BodyConstructionBuilder::BodyConstructionBuilder(
    const SemIRProgram& semantic,
    BodyID body_id
) noexcept
    : semantic(semantic),
      body(semantic.bodies().body(body_id)),
      failure_exit(ConstructionFunctionExit {}) {}

auto BodyConstructionBuilder::reserve_region() noexcept -> ConstructionRegionID {
    if (regions.size() >= std::numeric_limits<std::uint32_t>::max()) {
        invariant_violation("construction region limit exceeded");
    }
    const auto result = ConstructionRegionID(body.id(), static_cast<std::uint32_t>(regions.size()));
    regions.emplace_back();
    return result;
}

auto BodyConstructionBuilder::region(const SemanticRegion& source) noexcept
    -> ConstructionRegionID {
    const auto id = reserve_region();
    auto statements = std::vector<ConstructionStatement>();
    for (const auto& item : source.statements) {
        statements.push_back(statement(item));
    }
    auto result = std::optional<ConstructionExpressionID>();
    if (source.result) {
        result = expression(*source.result);
    }
    regions[id.index()] = ConstructionRegion {
        .lifetime = source.lifetime,
        .origin = source.origin,
        .statements = std::move(statements),
        .result = result
    };
    return id;
}

auto BodyConstructionBuilder::operand(
    const SemanticExpression& source,
    ConstructionUse use
) noexcept -> ConstructionOperand {
    return {.expression = expression(source), .use = use};
}

auto BodyConstructionBuilder::argument(const SemCallArgument& source) noexcept
    -> ConstructionOperand {
    auto use = ConstructionUse::ReadBorrow;
    if (source.access == AccessMode::Write) {
        use = ConstructionUse::Place;
    } else if (source.access == AccessMode::Take) {
        use = ConstructionUse::Consume;
    } else if (std::holds_alternative<PointerTypeValue>(
                   semantic.types().type(source.expression.type.resolved()).value
               )) {
        use = ConstructionUse::AddressValue;
    }
    return operand(source.expression, use);
}

auto BodyConstructionBuilder::finish() noexcept -> BodyConstruction {
    const auto root = region(body.region());
    auto complete_expressions = std::vector<ConstructionExpression>();
    complete_expressions.reserve(expressions.size());
    for (auto& value : expressions) {
        if (!value) {
            invariant_violation("construction published an unfinished expression");
        }
        complete_expressions.push_back(std::move(*value));
    }
    auto complete_regions = std::vector<ConstructionRegion>();
    complete_regions.reserve(regions.size());
    for (auto& value : regions) {
        if (!value) {
            invariant_violation("construction published an unfinished region");
        }
        complete_regions.push_back(std::move(*value));
    }
    auto result = BodyConstruction(
        body.id(),
        root,
        std::move(complete_expressions),
        std::move(complete_regions)
    );
    if (const auto checked = validate_construction(result, body); !checked) {
        const auto& error = checked.error();
        invariant_violation(
            std::format(
                "{} (body {}, source origin {})",
                error.message,
                body.id().index(),
                error.origin ? std::to_string(error.origin->index()) : "unknown"
            )
        );
    }
    return result;
}

auto construct_body(const SemIRProgram& semantic, BodyID body_id) noexcept -> BodyConstruction {
    return BodyConstructionBuilder(semantic, body_id).finish();
}
