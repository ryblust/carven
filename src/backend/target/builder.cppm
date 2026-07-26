module carven:backend.target.builder;

import :backend.target;
import :backend.target.expr;
import :backend.target.finalize;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
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
    TargetStorage storage;
};
