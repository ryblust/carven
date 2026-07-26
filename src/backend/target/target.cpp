module carven:backend.target.impl;

import :backend.target;
import std;

TargetUnit::TargetUnit(TargetStorage storage, TargetUnitRoot root) noexcept
    : storage(std::move(storage)),
      unit_root(std::move(root)) {}

auto TargetUnit::type(TargetTypeID id) const noexcept -> const TargetType& {
    return storage.types.get(id);
}
auto TargetUnit::expression(TargetExprID id) const noexcept -> const TargetExpr& {
    return storage.expressions.get(id);
}
auto TargetUnit::statement(TargetStmtID id) const noexcept -> const TargetStmt& {
    return storage.statements.get(id);
}
auto TargetUnit::item(TargetItemID id) const noexcept -> const TargetItem& {
    return storage.items.get(id);
}
auto TargetUnit::types() const noexcept -> std::span<const TargetType> {
    return storage.types.values();
}
auto TargetUnit::expressions() const noexcept -> std::span<const TargetExpr> {
    return storage.expressions.values();
}
auto TargetUnit::statements() const noexcept -> std::span<const TargetStmt> {
    return storage.statements.values();
}
auto TargetUnit::items() const noexcept -> std::span<const TargetItem> {
    return storage.items.values();
}

auto TargetUnit::root() const noexcept -> const TargetUnitRoot& {
    return unit_root;
}
