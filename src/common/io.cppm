export module zero.common.io;

import std;

export auto read_text_file(const std::filesystem::path& path) noexcept -> std::optional<std::string> {
    auto file = std::ifstream(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) return std::nullopt;

    const auto size = file.tellg();
    file.seekg(0);

    auto content = std::string(static_cast<std::size_t>(size), '\0');
    file.read(content.data(), size);
    if (!file.good()) return std::nullopt;

    return content;
}

export auto ensure_directory(const std::filesystem::path& dir) noexcept -> bool {
    auto error = std::error_code {};
    std::filesystem::create_directories(dir, error);

    return !error;
}

export auto write_text_file_if_changed(const std::filesystem::path& path, std::string_view content) noexcept -> bool {
    const auto existing = read_text_file(path);
    if (existing && *existing == content) return true;

    auto file = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) return false;

    file.write(content.data(), static_cast<std::streamsize>(content.size()));

    return file.good();
}
