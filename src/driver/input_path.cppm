module carven:driver.input_path;

import :source.module_path;
import std;

auto derive_input_module_path(std::string_view input_path) noexcept
    -> std::expected<CanonicalModulePath, std::string>;
