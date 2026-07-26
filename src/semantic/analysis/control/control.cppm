module carven:semantic.analysis.control;

import :semantic.analysis.builder;
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

class SolvedControl final {
public:
    SolvedControl(const SolvedControl&) = delete;
    SolvedControl(SolvedControl&&) = default;
    auto operator=(const SolvedControl&) -> SolvedControl& = delete;
    auto operator=(SolvedControl&&) -> SolvedControl& = default;
    ~SolvedControl() = default;

    auto summary(HIRExprID id) const noexcept -> const ControlSummary&;
    auto summary(HIRStmtID id) const noexcept -> const ControlSummary&;
    auto summary(HIRBlockID id) const noexcept -> const ControlSummary&;
    auto catch_failures(HIRExprID id, std::size_t arm) const noexcept -> std::span<const HIRTypeID>;
    auto unhandled_failures(HIRExprID id) const noexcept -> std::span<const HIRTypeID>;
    auto evaluation_effect(HIRExprID id) const noexcept -> const EvaluationEffect&;

private:
    SolvedControl(
        std::vector<ControlSummary> expressions,
        std::vector<ControlSummary> statements,
        std::vector<ControlSummary> blocks,
        std::vector<std::vector<std::vector<HIRTypeID>>> catches,
        std::vector<std::vector<HIRTypeID>> unhandled,
        std::vector<EvaluationEffect> effects
    ) noexcept;

    std::vector<ControlSummary> expression_summaries;
    std::vector<ControlSummary> statement_summaries;
    std::vector<ControlSummary> block_summaries;
    std::vector<std::vector<std::vector<HIRTypeID>>> catch_summaries;
    std::vector<std::vector<HIRTypeID>> unhandled_summaries;
    std::vector<EvaluationEffect> expression_effects;

    friend auto solve_control(SemanticConstruction&) noexcept -> SolvedControl;
    friend auto commit_control_facts(SemanticConstruction&, SolvedControl&&) noexcept -> void;
};

auto solve_control(SemanticConstruction& builder) noexcept -> SolvedControl;
auto commit_control_facts(SemanticConstruction& builder, SolvedControl&& control) noexcept -> void;
