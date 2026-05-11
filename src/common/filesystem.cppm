export module zero.common.filesystem;

import std;

export auto read_file(const std::filesystem::path& path) noexcept -> std::optional<std::string>;

export auto ensure_directory(const std::filesystem::path& dir) noexcept -> bool;

export auto write_file_if_changed(const std::filesystem::path& path, std::string_view content) noexcept -> bool;
