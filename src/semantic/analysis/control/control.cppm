module carven:semantic.analysis.control;

import :semantic.analysis.session;
import :semantic.analysis.session.read;
import :semantic.hir;
import :semantic.hir.place;
import std;

struct ControlTransfers final {
    bool returns;
    bool breaks;
    bool continues;
    bool exits_test;
    constexpr auto operator==(const ControlTransfers&) const noexcept -> bool = default;
};

struct ControlSummary final {
    bool falls_through;
    ControlTransfers transfers;
    std::vector<HIRTypeID> pending_failures;
    std::vector<HIRTypeID> outward_failures;
    constexpr auto operator==(const ControlSummary&) const noexcept -> bool = default;
};

enum class CatchAlternativeReachability {
    Reachable,
    FailureAbsent,
    Covered,
};

struct CatchControlSummary final {
    std::vector<HIRTypeID> accepted_failures;
    std::vector<CatchAlternativeReachability> alternatives;
};

class RecordedControlAnalysis final {
public:
    RecordedControlAnalysis(const RecordedControlAnalysis&) = delete;
    RecordedControlAnalysis(RecordedControlAnalysis&&) = default;
    auto operator=(const RecordedControlAnalysis&) -> RecordedControlAnalysis& = delete;
    auto operator=(RecordedControlAnalysis&&) -> RecordedControlAnalysis& = default;
    ~RecordedControlAnalysis() = default;

    static auto for_testing(
        std::vector<ControlSummary> expressions,
        std::vector<ControlSummary> statements,
        std::vector<ControlSummary> blocks,
        std::vector<std::vector<CatchControlSummary>> catches,
        std::vector<std::vector<HIRTypeID>> unhandled,
        std::vector<EvaluationEffect> effects,
        std::vector<std::vector<HIRTypeID>> callable_failures
    ) noexcept -> RecordedControlAnalysis;

    auto summary(HIRExprID id) const noexcept -> const ControlSummary&;
    auto summary(HIRStmtID id) const noexcept -> const ControlSummary&;
    auto summary(HIRBlockID id) const noexcept -> const ControlSummary&;
    auto catch_summary(HIRExprID id, std::size_t arm) const noexcept -> const CatchControlSummary&;
    auto unhandled_failures(HIRExprID id) const noexcept -> std::span<const HIRTypeID>;
    auto evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect&;
    auto effective_failures(CallableID id) const noexcept -> std::span<const HIRTypeID>;
    auto callable_failure_sets() const noexcept -> std::span<const std::vector<HIRTypeID>>;

private:
    RecordedControlAnalysis(
        std::vector<ControlSummary> expressions,
        std::vector<ControlSummary> statements,
        std::vector<ControlSummary> blocks,
        std::vector<std::vector<CatchControlSummary>> catches,
        std::vector<std::vector<HIRTypeID>> unhandled,
        std::vector<EvaluationEffect> effects,
        std::vector<std::vector<HIRTypeID>> callable_failures
    ) noexcept;

    std::vector<ControlSummary> expression_summaries;
    std::vector<ControlSummary> statement_summaries;
    std::vector<ControlSummary> block_summaries;
    std::vector<std::vector<CatchControlSummary>> catch_summaries;
    std::vector<std::vector<HIRTypeID>> unhandled_summaries;
    std::vector<EvaluationEffect> expression_effects;
    std::vector<std::vector<HIRTypeID>> effective_callable_failures;

    friend auto analyze_control(SemanticDraftView) noexcept -> RecordedControlAnalysis;
    friend auto freeze_flow_candidate(SemanticDraft&, RecordedControlAnalysis&&) noexcept -> void;
};

auto analyze_control(SemanticDraftView builder) noexcept -> RecordedControlAnalysis;
auto freeze_flow_candidate(SemanticDraft& builder, RecordedControlAnalysis&& control) noexcept
    -> void;
