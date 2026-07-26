module carven:backend.target;

import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import :support.id_table;
import std;

class TargetUnit;
class TargetUnitBuilder;
class TargetUnitFinalizer;

class TargetStorage final {
    friend class TargetUnitBuilder;
    friend class TargetUnitFinalizer;
    friend class TargetUnit;

    TargetStorage() = default;
    TargetStorage(const TargetStorage&) = delete;
    TargetStorage(TargetStorage&&) = default;
    ~TargetStorage() = default;

    auto operator=(const TargetStorage&) -> TargetStorage& = delete;
    auto operator=(TargetStorage&&) -> TargetStorage& = default;

    IDTable<TargetType, TargetTypeID> types;
    IDTable<TargetExpr, TargetExprID> expressions;
    IDTable<TargetStmt, TargetStmtID> statements;
    IDTable<TargetItem, TargetItemID> items;
};

class TargetUnit final {
public:
    TargetUnit(const TargetUnit&) = delete;
    TargetUnit(TargetUnit&&) = default;
    ~TargetUnit() = default;

    auto operator=(const TargetUnit&) -> TargetUnit& = delete;
    auto operator=(TargetUnit&&) -> TargetUnit& = default;

    auto type(TargetTypeID id) const noexcept -> const TargetType&;
    auto expression(TargetExprID id) const noexcept -> const TargetExpr&;
    auto statement(TargetStmtID id) const noexcept -> const TargetStmt&;
    auto item(TargetItemID id) const noexcept -> const TargetItem&;
    auto types() const noexcept -> std::span<const TargetType>;
    auto expressions() const noexcept -> std::span<const TargetExpr>;
    auto statements() const noexcept -> std::span<const TargetStmt>;
    auto items() const noexcept -> std::span<const TargetItem>;
    auto root() const noexcept -> const TargetUnitRoot&;

private:
    friend class TargetUnitBuilder;
    friend class TargetUnitFinalizer;

    explicit TargetUnit(TargetStorage storage, TargetUnitRoot root) noexcept;

    TargetStorage storage;
    TargetUnitRoot unit_root;
};
