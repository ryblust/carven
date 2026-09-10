module carven:backend.construction.builder;

import :backend.construction;
import :semantic.semir;
import std;

class BodyConstructionBuilder final {
public:
    BodyConstructionBuilder(const SemIRProgram& semantic, BodyID body_id) noexcept;
    auto finish() noexcept -> BodyConstruction;

private:
    auto expression(const SemanticExpression& source) noexcept -> ConstructionExpressionID;
    auto operand(const SemanticExpression& source, ConstructionUse use) noexcept
        -> ConstructionOperand;
    auto argument(const SemCallArgument& source) noexcept -> ConstructionOperand;
    auto operands(const SemanticExpression& source) noexcept -> std::vector<ConstructionOperand>;
    auto region(const SemanticRegion& source) noexcept -> ConstructionRegionID;
    auto statement(const SemanticStatement& source) noexcept -> ConstructionStatement;
    auto reserve_region() noexcept -> ConstructionRegionID;

    const SemIRProgram& semantic;
    const SemIRBody& body;
    ConstructionFailureExit failure_exit;
    std::optional<ConstructionCaughtFailure> caught;
    std::optional<ConstructionRegionID> loop;
    std::vector<std::optional<ConstructionExpression>> expressions;
    std::vector<std::optional<ConstructionRegion>> regions;
    std::unordered_map<const SemanticExpression*, ConstructionExpressionID> source_expressions;
};
