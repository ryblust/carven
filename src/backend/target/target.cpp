module carven:backend.target.impl;

import :backend.target;
import :support.invariant;
import std;

TargetUnit::TargetUnit(
    TargetUnitIdentity unit_identity,
    std::vector<TargetType> types,
    TargetUnitContents unit_contents
) noexcept
    : unit_identity(unit_identity),
      target_types(std::move(types)),
      contents(std::move(unit_contents)),
      active(true) {}

TargetUnit::TargetUnit(TargetUnit&& other) noexcept
    : unit_identity(other.unit_identity),
      target_types(std::move(other.target_types)),
      contents(std::move(other.contents)),
      active(std::exchange(other.active, false)) {}

auto TargetUnit::require_active() const noexcept -> void {
    if (!active) {
        invariant_violation("target unit was used after move");
    }
}

auto TargetUnit::identity() const noexcept -> TargetUnitIdentity {
    require_active();
    return unit_identity;
}

auto TargetUnit::type(TargetTypeID id) const noexcept -> const TargetType& {
    require_active();
    if (id.owner() != unit_identity || id.index() >= target_types.size()) {
        invariant_violation("target unit type lookup used a foreign or invalid identity");
    }
    return target_types[id.index()];
}

auto TargetUnit::type_count() const noexcept -> std::size_t {
    require_active();
    return target_types.size();
}

auto TargetUnit::directive_groups() const noexcept -> std::span<const TargetDirectiveGroup> {
    require_active();
    return contents.directive_groups;
}

auto TargetUnit::sections() const noexcept -> const TargetUnitSections& {
    require_active();
    return contents.sections;
}
