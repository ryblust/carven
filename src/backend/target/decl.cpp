module carven:backend.target.decl.impl;

import :backend.target.decl;
import std;

auto target_parameters(TargetParameter parameter) noexcept -> std::vector<TargetParameter> {
    auto result = std::vector<TargetParameter>();
    result.push_back(std::move(parameter));
    return result;
}

auto target_parameters(TargetParameter first, TargetParameter second) noexcept
    -> std::vector<TargetParameter> {
    auto result = std::vector<TargetParameter>();
    result.reserve(2);
    result.push_back(std::move(first));
    result.push_back(std::move(second));
    return result;
}
