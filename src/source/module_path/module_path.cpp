module carven:source.module_path.impl;

import :source.identifier;
import :source.module_path;
import :support.path;
import :support.utf8;
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
        if (!is_identifier_spelling(component)) {
            return std::unexpected(
                CanonicalModulePathError {
                    .kind = CanonicalModulePathErrorKind::InvalidIdentifier,
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

auto CanonicalModulePath::from_source_path(std::string_view input_path) noexcept
    -> std::expected<CanonicalModulePath, std::string> {
    if (input_path.empty()) {
        return std::unexpected("input path cannot be empty");
    }
    if (input_path.contains('\0')) {
        return std::unexpected("input path cannot contain NUL");
    }
    if (input_path.contains('\\')) {
        return std::unexpected("input path must use '/' as the path separator");
    }
    if (input_path.contains('"')) {
        return std::unexpected("input path cannot contain '\"'");
    }
    if (input_path.contains('\n') || input_path.contains('\r')) {
        return std::unexpected("input path cannot contain a line break");
    }
    if (!UTF8Decoder::is_valid(input_path)) {
        return std::unexpected("input path is not valid UTF-8");
    }

    auto source_path = path_from_utf8(input_path).lexically_normal();

    if (source_path.is_absolute()
        || source_path.has_root_name()
        || source_path.has_root_directory()) {
        return std::unexpected(std::format("input '{}' must be relative", input_path));
    }

    if (source_path.extension() != path_from_utf8(".cv")) {
        return std::unexpected(std::format("input '{}' does not have a .cv extension", input_path));
    }

    source_path.replace_extension();
    auto value = path_to_generic_utf8(source_path);
    if (value.empty() || value == ".") {
        return std::unexpected(std::format("cannot derive a module path from '{}'", input_path));
    }

    auto components = std::vector<std::string_view> {};
    for (auto start = 0uz; start <= value.size();) {
        const auto separator = value.find('/', start);

        const auto end = separator == std::string::npos ? value.size() : separator;
        const auto component = std::string_view(value).substr(start, end - start);

        if (component.empty() || component == ".") {
            return std::unexpected(
                std::format("input '{}' derives an empty module path component", input_path)
            );
        }

        if (component == "..") {
            return std::unexpected(std::format("input '{}' escapes the source root", input_path));
        }

        components.push_back(component);
        if (separator == std::string::npos) {
            break;
        }

        start = separator + 1;
    }

    auto module_path = CanonicalModulePath::from_components(components);
    if (module_path.has_value()) {
        return std::move(*module_path);
    }

    const auto& error = module_path.error();
    const auto is_file_stem = error.component_index + 1 == components.size();
    return std::unexpected(
        std::format(
            "input '{}' has invalid module {} '{}'",
            input_path,
            is_file_stem ? "file stem" : "directory component",
            error.component
        )
    );
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
