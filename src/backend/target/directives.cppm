module carven:backend.target.directives;

import :backend.target.header;
import :backend.target.unit;
import std;

auto plan_target_directives(
    TargetDirectiveInputs inputs,
    std::span<const TargetHeaderRequirement> dependencies
) noexcept -> std::vector<TargetDirectiveGroup>;
