module carven:backend.target.dependencies;

import :backend.target.type;
import :backend.target.unit;
import std;

auto collect_target_dependencies(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& sections
) noexcept -> std::vector<TargetDirective>;
