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
    TargetUnitBuilder(TargetUnitBuilder&&) noexcept = default;
    ~TargetUnitBuilder() = default;
    auto operator=(const TargetUnitBuilder&) -> TargetUnitBuilder& = delete;
    auto operator=(TargetUnitBuilder&&) -> TargetUnitBuilder& = delete;
    auto identity() const noexcept -> TargetUnitIdentity;
    auto local_name(TargetLocalID id) const noexcept -> const TargetIdentifier&;
    auto add_local(TargetIdentifier name) noexcept -> TargetLocalID;
    auto intern_type(TargetType type) noexcept -> TargetTypeID;
    // The type uses namespace-visible names only. Its alias is placed before its first use.
    auto name_namespace_type(TargetTypeID type, const TargetIdentifier& name) noexcept -> void;
    auto finish(TargetUnitSections sections, TargetDirectiveInputs directives = {}) && noexcept
        -> TargetUnit;

private:
    TargetUnitIdentity unit_identity;
    std::vector<TargetType> types;
    std::vector<TargetIdentifier> locals;
    std::map<TargetTypeID, TargetIdentifier> namespace_types;
    std::unordered_multimap<std::size_t, std::uint32_t> type_candidates;
};

auto target_lowering_statement(TargetStmtValue value) noexcept -> TargetStmt;
auto target_lowering_item(TargetItemValue value) noexcept -> TargetItem;
