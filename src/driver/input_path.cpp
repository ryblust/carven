module carven:driver.input_path.impl;

import :driver.input_path;
import :source.module_path;
import :support.path;
import :support.utf8;
import std;

auto derive_input_module_path(std::string_view input_path) noexcept
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
            return std::unexpected(
                std::format("input '{}' escapes the working directory", input_path)
            );
        }

        components.push_back(component);
        if (separator == std::string::npos) {
            break;
        }

        start = separator + 1;
    }

    auto module_path = CanonicalModulePath::from_components(components);
    if (module_path.has_value()) {
        const auto craft_name = module_path->module_domain_prefix().craft_name();
        if (craft_name.has_value() && *craft_name == "std") {
            return std::unexpected(
                std::format(
                    "input '{}' resolves to the toolchain-reserved 'crafts.std' module domain",
                    input_path
                )
            );
        }
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
