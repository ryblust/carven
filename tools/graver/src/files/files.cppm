module carven:graver.files;

import std;

namespace graver {

// Directory inputs expand to .cv files, excluding hidden/build directories and
// symlinks. I/O paths are absolute and preserve .. components. Deduplication
// resolves parent directories only; final file symlinks retain their identity.
// Returned paths are sorted and reused for diagnostics and writes.
auto collect_inputs(std::span<const std::string_view> inputs) noexcept
    -> std::expected<std::vector<std::filesystem::path>, std::string>;

// Stage in the destination directory and replace only after successful write.
// Refuses symlinks and detects edits made since the caller read the input.
auto replace_file(
    const std::filesystem::path& path,
    std::string_view original,
    std::string_view formatted
) noexcept -> std::expected<void, std::string>;

}
