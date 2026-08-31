module carven:backend.target.builder;

import :backend.target;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import std;

class TargetUnitBuilder final {
public:
    TargetUnitBuilder() = default;
    TargetUnitBuilder(const TargetUnitBuilder&) = delete;
    TargetUnitBuilder(TargetUnitBuilder&&) = default;
    ~TargetUnitBuilder() = default;

    auto operator=(const TargetUnitBuilder&) -> TargetUnitBuilder& = delete;
    auto operator=(TargetUnitBuilder&&) -> TargetUnitBuilder& = default;

    auto intern_type(TargetType type) noexcept -> TargetTypeID;
    auto append_expression(TargetExpr expression) noexcept -> TargetExprID;
    auto clone_expression_occurrence(TargetExprID expression_id) noexcept -> TargetExprID;
    auto append_statement(TargetStmt statement) noexcept -> TargetStmtID;
    auto append_lowering_statement(TargetStmtValue value) noexcept -> TargetStmtID;
    auto append_item(TargetItem item) noexcept -> TargetItemID;
    auto append_lowering_item(TargetItemValue value) noexcept -> TargetItemID;

    auto expression(TargetExprID id) const noexcept -> const TargetExpr&;
    auto statement(TargetStmtID id) const noexcept -> const TargetStmt&;
    auto item(TargetItemID id) const noexcept -> const TargetItem&;
    auto type(TargetTypeID id) const noexcept -> const TargetType&;

    auto finish(TargetUnitRoot root) && noexcept -> TargetUnit;

private:
    struct CloneActivePath final {
        std::vector<std::uint8_t> expressions;
        std::vector<std::uint8_t> statements;
    };

    auto clone_expression_occurrence(
        TargetExprID expression_id,
        CloneActivePath& active_path
    ) noexcept -> TargetExprID;

    auto clone_statement_occurrence(
        TargetStmtID statement_id,
        CloneActivePath& active_path
    ) noexcept -> TargetStmtID;

    auto clone_expression_occurrences(
        std::span<const TargetExprID> expression_ids,
        CloneActivePath& active_path
    ) noexcept -> std::vector<TargetExprID>;

    auto clone_statement_occurrences(
        std::span<const TargetStmtID> statement_ids,
        CloneActivePath& active_path
    ) noexcept -> std::vector<TargetStmtID>;

    TargetStorage storage;
};
