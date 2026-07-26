module carven:support.file;

import std;

enum class FileOperation {
    Open,
    Inspect,
    Read,
    Write,
    Flush,
    Close,
};

struct FileError final {
    FileOperation operation;
    std::error_code code;
};

[[nodiscard]] auto read_file(const std::filesystem::path& path, std::size_t max_bytes) noexcept
    -> std::expected<std::string, FileError>;

auto write_file(const std::filesystem::path& path, std::string_view content) noexcept
    -> std::expected<void, FileError>;
