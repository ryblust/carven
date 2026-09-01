module carven:semantic.analysis.availability.build;

import :semantic.analysis.availability.graph;
import :semantic.analysis.availability.place;
import :semantic.analysis.session.read;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.stmt;
import :semantic.hir.type;
import std;

struct AvailabilityTargets final {
    AvailabilityBlockID returned;
    AvailabilityBlockID broken;
    AvailabilityBlockID continued;
    AvailabilityBlockID test_exit;
    AvailabilityBlockID failure_fallback;
    std::vector<std::pair<HIRTypeID, AvailabilityBlockID>> failures;
    std::vector<HIRTypeID> rethrows;
};

class BodyAvailabilityGraphBuilder final {
public:
    BodyAvailabilityGraphBuilder(
        SemanticDraftView hir,
        const AvailabilityPlaceCatalog& catalog,
        BodyID body
    ) noexcept;

    auto build() && noexcept -> BodyAvailabilityGraph;

private:
    auto local(SymbolID symbol) const noexcept -> AvailabilityPlaceID;
    auto required_place(SymbolID symbol) const noexcept -> AvailabilityPlaceID;
    auto whole_place(HIRExprID expression) const noexcept -> std::optional<SymbolID>;
    auto prepend(AvailabilityOperation operation, AvailabilityBlockID next) noexcept
        -> AvailabilityBlockID;
    auto branch(std::vector<AvailabilityBlockID> successors) noexcept -> AvailabilityBlockID;
    auto failure_target(HIRTypeID failure, const AvailabilityTargets& targets) const noexcept
        -> AvailabilityBlockID;
    auto set_failure_target(
        AvailabilityTargets& targets,
        HIRTypeID failure,
        AvailabilityBlockID target
    ) const noexcept -> void;
    auto failure_branch(
        std::span<const HIRTypeID> failures,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;
    auto call_failures(HIRExprID call) const noexcept -> std::span<const HIRTypeID>;
    auto add_locals(
        std::vector<AvailabilityPlaceID>& destination,
        std::span<const SymbolID> places
    ) const noexcept -> void;
    auto remove_local(
        std::vector<AvailabilityPlaceID>& places,
        AvailabilityPlaceID place
    ) const noexcept -> void;
    auto call_check(HIRExprID id, const HIRCallExpr& call) const noexcept
        -> AvailabilityAccessCheck;
    auto assignment_checks(const HIRAssignmentStmt& assignment) const noexcept
        -> std::array<AvailabilityAccessCheck, 2>;
    auto append_access(HIRExprID id, AvailabilityBlockID next) noexcept -> AvailabilityBlockID;
    auto pattern_places(
        HIRPatternID pattern,
        std::vector<AvailabilityPlaceID>& places
    ) const noexcept -> void;
    auto restore_patterns(std::span<const HIRPatternID> patterns, AvailabilityBlockID next) noexcept
        -> AvailabilityBlockID;

    auto lower_conditional(
        std::span<const HIRConditionalBranch> branches,
        std::optional<HIRBlockID> other,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;
    auto lower_match(
        HIRExprID subject,
        std::span<const HIRMatchArm> arms,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;
    auto lower_try(
        HIRExprID id,
        const HIRTryExpr& attempt,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;
    auto lower_expression(
        HIRExprID id,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;

    auto lower_while(
        const HIRWhileStmt& loop,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;
    auto lower_c_style_for(
        const HIRCStyleForStmt& loop,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;
    auto lower_range_for(
        const HIRRangeForStmt& loop,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;
    auto lower_statement(
        HIRStmtID id,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;
    auto lower_block(
        HIRBlockID id,
        AvailabilityBlockID normal,
        const AvailabilityTargets& targets
    ) noexcept -> AvailabilityBlockID;

    SemanticDraftView hir;
    const AvailabilityPlaceCatalog& catalog;
    BodyID body;
    MutableAvailabilityGraph graph;
};
