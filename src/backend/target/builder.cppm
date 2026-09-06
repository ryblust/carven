module carven:backend.target.builder;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import :backend.target;
import std;

class TargetUnitBuilder final {
public:
    TargetUnitBuilder() noexcept;
    TargetUnitBuilder(const TargetUnitBuilder&) = delete;
    TargetUnitBuilder(TargetUnitBuilder&& other) noexcept;
    ~TargetUnitBuilder() = default;

    auto operator=(const TargetUnitBuilder&) -> TargetUnitBuilder& = delete;
    auto operator=(TargetUnitBuilder&&) -> TargetUnitBuilder& = delete;

    auto identity() const noexcept -> TargetUnitIdentity;
    auto intern_type(TargetType type) noexcept -> TargetTypeID;

    auto finish(TargetUnitSections sections, TargetDirectiveInputs directives = {}) && noexcept
        -> TargetUnit;

private:
    auto require_identity() const noexcept -> TargetUnitIdentity;

    std::optional<TargetUnitIdentity> unit_identity;
    std::vector<TargetType> types;
};

auto target_lowering_statement(TargetStmtValue value) noexcept -> TargetStmt;
auto target_lowering_item(TargetItemValue value) noexcept -> TargetItem;
