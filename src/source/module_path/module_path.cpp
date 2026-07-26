module carven:source.module_path.impl;

import :source.identifier;
import :source.module_path;
import std;

auto CanonicalModulePath::from_value(std::string_view value) noexcept
    -> std::expected<CanonicalModulePath, CanonicalModulePathError> {
    if (value.empty()) {
        return from_components({});
    }
    auto components = std::vector<std::string_view>();
    for (auto start = 0uz; start <= value.size();) {
        const auto separator = value.find('.', start);
        const auto end = separator == std::string_view::npos ? value.size() : separator;
        components.push_back(value.substr(start, end - start));
        if (separator == std::string_view::npos) {
            break;
        }
        start = separator + 1;
    }
    return from_components(components);
}

auto CanonicalModulePath::from_components(std::span<const std::string_view> components) noexcept
    -> std::expected<CanonicalModulePath, CanonicalModulePathError> {
    if (components.empty()) {
        return std::unexpected(
            CanonicalModulePathError {
                .kind = CanonicalModulePathErrorKind::Empty,
                .component_index = 0,
                .component = {},
            }
        );
    }

    auto value = std::string {};
    auto owned_components = std::vector<std::string>();
    owned_components.reserve(components.size());
    for (auto index = 0uz; index < components.size(); ++index) {
        const auto component = components[index];
        if (component.empty()) {
            return std::unexpected(
                CanonicalModulePathError {
                    .kind = CanonicalModulePathErrorKind::EmptyComponent,
                    .component_index = index,
                    .component = {},
                }
            );
        }
        const auto classification = classify_identifier(component);
        if (std::holds_alternative<InvalidIdentifier>(classification)) {
            return std::unexpected(
                CanonicalModulePathError {
                    .kind = CanonicalModulePathErrorKind::InvalidIdentifier,
                    .component_index = index,
                    .component = std::string(component),
                }
            );
        }
        if (std::holds_alternative<KeywordIdentifier>(classification)) {
            return std::unexpected(
                CanonicalModulePathError {
                    .kind = CanonicalModulePathErrorKind::Keyword,
                    .component_index = index,
                    .component = std::string(component),
                }
            );
        }
        if (!value.empty()) {
            value += '.';
        }
        value += component;
        owned_components.emplace_back(component);
    }
    if (components.front() == "crafts" && components.size() < 3) {
        return std::unexpected(
            CanonicalModulePathError {
                .kind = CanonicalModulePathErrorKind::IncompleteCraftPath,
                .component_index = components.size(),
                .component = value,
            }
        );
    }
    return CanonicalModulePath(std::move(value), std::move(owned_components));
}

auto CanonicalModulePath::value() const noexcept -> std::string_view {
    return path;
}

auto CanonicalModulePath::components() const noexcept -> std::span<const std::string> {
    return path_components;
}

auto CanonicalModulePath::module_domain_prefix() const noexcept -> ModuleDomainPrefix {
    if (path_components.front() == "crafts") {
        return ModuleDomainPrefix(std::optional<std::string> {path_components[1]});
    }
    return ModuleDomainPrefix(std::nullopt);
}

auto CanonicalModulePath::domain_relative_components() const noexcept
    -> std::span<const std::string> {
    return path_components.front() == "crafts" ? std::span(path_components).subspan(2)
                                               : std::span(path_components);
}

auto CanonicalModulePath::operator==(const CanonicalModulePath& other) const noexcept -> bool {
    return path_components == other.path_components;
}

auto CanonicalModulePath::operator<=>(const CanonicalModulePath& other) const noexcept
    -> std::strong_ordering {
    return path_components <=> other.path_components;
}

CanonicalModulePath::CanonicalModulePath(
    std::string value,
    std::vector<std::string> components
) noexcept
    : path(std::move(value)),
      path_components(std::move(components)) {}

ModuleDomainPrefix::ModuleDomainPrefix(std::optional<std::string> value) noexcept
    : name(std::move(value)) {}

auto ModuleDomainPrefix::craft_name() const noexcept -> std::optional<std::string_view> {
    return name.has_value() ? std::optional<std::string_view>(*name) : std::nullopt;
}

auto same_module_domain(const CanonicalModulePath& left, const CanonicalModulePath& right) noexcept
    -> bool {
    return left.module_domain_prefix() == right.module_domain_prefix();
}
