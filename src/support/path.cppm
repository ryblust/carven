module carven:support.path;

import std;

auto path_from_utf8(std::string_view value) noexcept -> std::filesystem::path;

auto path_to_generic_utf8(const std::filesystem::path& path) noexcept -> std::string;
