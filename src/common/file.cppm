export module zero.common.file;

import std;

export auto read_file(const std::filesystem::path& path) noexcept -> std::optional<std::string> {
    auto error = std::error_code();
    const auto size = std::filesystem::file_size(path, error);
    if (error) return std::nullopt;

    auto file = std::ifstream(path, std::ios::binary);
    if (!file.is_open()) return std::nullopt;

    auto content = std::string(static_cast<std::size_t>(size), '\0');
    file.read(content.data(), static_cast<std::streamsize>(size));
    if (!file.good()) return std::nullopt;

    return content;
}

export auto ensure_directory(const std::filesystem::path& dir) noexcept -> bool {
    auto error = std::error_code();
    std::filesystem::create_directories(dir, error);

    return !error;
}

export auto write_file_if_changed(const std::filesystem::path& path, std::string_view content) noexcept -> bool {
    const auto existing = read_file(path);
    if (existing && *existing == content) return true;

    auto file = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file.write(content.data(), static_cast<std::streamsize>(content.size()));

    return file.good();
}
