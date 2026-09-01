module carven:semantic.analysis.availability.solve;

import :semantic.analysis.availability.graph;
import :semantic.analysis.availability.place;
import :semantic.analysis.session.read;
import :semantic.hir.ids;
import :source.provenance;
import std;

struct UnavailableUseFinding final {
    HIRExprID expression;
    TakeSite site;
};

struct OperationConflictFinding final {
    HIRExprID primary;
    std::optional<HIRExprID> related;
};

struct InvalidTakeFinding final {
    ProgramOriginID origin;
    InvalidTakeKind kind;
};

struct CaptureConflictFinding final {
    TakeSite take_site;
    ProgramOriginID capture_origin;
};

struct BodyAvailabilityFindings final {
    std::vector<UnavailableUseFinding> unavailable_uses;
    std::vector<OperationConflictFinding> operation_conflicts;
    std::vector<InvalidTakeFinding> invalid_takes;
    std::vector<CaptureConflictFinding> capture_conflicts;
};

auto solve_body_availability(
    SemanticDraftView hir,
    const AvailabilityPlaceCatalog& catalog,
    BodyID body,
    const BodyAvailabilityGraph& graph
) noexcept -> BodyAvailabilityFindings;
