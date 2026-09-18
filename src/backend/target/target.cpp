module carven:backend.target.impl;

import :backend.target;
import :support.invariant;
import std;

TargetUnit::TargetUnit(
    TargetUnitIdentity unit_identity,
    std::vector<TargetType> types,
    std::vector<TargetIdentifier> locals,
    TargetUnitContents unit_contents
) noexcept
    : unit_identity(unit_identity),
      target_types(std::move(types)),
      locals(std::move(locals)),
      contents(std::move(unit_contents)) {}

auto TargetUnit::identity() const noexcept -> TargetUnitIdentity {
    return unit_identity;
}

auto TargetUnit::type(TargetTypeID id) const noexcept -> const TargetType& {
    if (id.owner() != unit_identity || id.index() >= target_types.size()) {
        invariant_violation("target unit type lookup used a foreign or invalid identity");
    }
    return target_types[id.index()];
}

auto TargetUnit::type_count() const noexcept -> std::size_t {
    return target_types.size();
}

auto TargetUnit::directive_groups() const noexcept -> std::span<const TargetDirectiveGroup> {
    return contents.directive_groups;
}

auto TargetUnit::sections() const noexcept -> const TargetUnitSections& {
    return contents.sections;
}

auto TargetUnit::local_name(TargetLocalID id) const noexcept -> const TargetIdentifier& {
    if (id.owner() != unit_identity || id.index() >= locals.size()) {
        invariant_violation("target local lookup used a foreign or invalid identity");
    }
    return locals[id.index()];
}
