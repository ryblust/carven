module carven:backend.target.name.impl;

import :backend.target.name;
import :source.cpp.identifier;
import :support.invariant;
import std;

auto TargetIdentifier::accepts_spelling(std::string_view spelling) noexcept -> bool {
    return is_supported_cpp_identifier(spelling);
}

auto TargetIdentifier::from_spelling(std::string_view spelling) noexcept -> TargetIdentifier {
    if (!accepts_spelling(spelling)) {
        invariant_violation(std::format("target identifier spelling '{}' is invalid", spelling));
    }
    return TargetIdentifier(std::string(spelling));
}

TargetIdentifier::TargetIdentifier(std::string spelling) noexcept
    : value(std::move(spelling)) {}

auto TargetIdentifier::spelling() const noexcept -> std::string_view {
    return value;
}

TargetName::TargetName(TargetIdentifier identifier) noexcept
    : TargetName(std::vector<TargetIdentifier> {std::move(identifier)}, false) {}

auto TargetName::from_components(std::initializer_list<TargetIdentifier> values) noexcept
    -> TargetName {
    return TargetName(std::vector<TargetIdentifier>(values), false);
}

auto TargetName::from_components(std::vector<TargetIdentifier> values) noexcept -> TargetName {
    return TargetName(std::move(values), false);
}

auto TargetName::globally_qualified(std::vector<TargetIdentifier> values) noexcept -> TargetName {
    return TargetName(std::move(values), true);
}

TargetName::TargetName(std::vector<TargetIdentifier> components, bool globally_qualified) noexcept
    : name_components(std::move(components)),
      global_qualification(globally_qualified) {
    if (name_components.empty()) {
        invariant_violation("target qualified name requires at least one component");
    }
}

auto TargetName::components() const noexcept -> std::span<const TargetIdentifier> {
    return name_components;
}

auto TargetName::is_globally_qualified() const noexcept -> bool {
    return global_qualification;
}

auto TargetName::append(TargetIdentifier identifier) noexcept -> void {
    name_components.push_back(std::move(identifier));
}
