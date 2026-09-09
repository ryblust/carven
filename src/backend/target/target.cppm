module carven:backend.target;

import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import std;

class TargetUnit;
class TargetUnitBuilder;

class TargetUnit final {
public:
    TargetUnit(const TargetUnit&) = delete;
    TargetUnit(TargetUnit&&) noexcept = default;
    ~TargetUnit() = default;

    auto operator=(const TargetUnit&) -> TargetUnit& = delete;
    auto operator=(TargetUnit&&) -> TargetUnit& = delete;

    auto identity() const noexcept -> TargetUnitIdentity;
    auto type(TargetTypeID id) const noexcept -> const TargetType&;
    auto type_count() const noexcept -> std::size_t;
    auto directive_groups() const noexcept -> std::span<const TargetDirectiveGroup>;
    auto sections() const noexcept -> const TargetUnitSections&;

private:
    friend class TargetUnitBuilder;


    TargetUnit(
        TargetUnitIdentity identity,
        std::vector<TargetType> types,
        TargetUnitContents contents
    ) noexcept;

    TargetUnitIdentity unit_identity;
    std::vector<TargetType> target_types;
    TargetUnitContents contents;
};
