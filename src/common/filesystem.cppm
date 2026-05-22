export module carven.common.filesystem;

import std;

export auto read_file(const std::filesystem::path& path) noexcept -> std::optional<std::string> {
    auto file = std::ifstream(path, std::ios::binary | std::ios::ate);
    if (!file) return std::nullopt;

    const auto size = static_cast<std::streamsize>(file.tellg());
    if (size == -1) return std::nullopt;

    auto buffer = std::string(static_cast<std::size_t>(size), '\0');
    file.seekg(0);
    file.read(buffer.data(), size);
    if (file.gcount() != size) return std::nullopt;

    return buffer;
}

export auto ensure_directory(const std::filesystem::path& dir) noexcept -> bool {
    auto error = std::error_code();
    std::filesystem::create_directories(dir, error);
    return !error;
}

export auto write_file_if_changed(const std::filesystem::path& path, std::string_view content) noexcept -> bool {
    if (const auto existing = read_file(path); existing && *existing == content) {
        return true;
    }

    if (auto file = std::ofstream(path, std::ios::binary | std::ios::trunc); file.is_open()) {
        return file.write(content.data(), content.size()).good();
    } else {
        return false;
    }
}
