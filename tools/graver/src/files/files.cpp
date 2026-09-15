module carven:graver.files.impl;

import :graver.files;
import :support.file;
import :support.path;
import std;

namespace {

class StagingDirectory final {
public:
    explicit StagingDirectory(std::filesystem::path path) noexcept;
    StagingDirectory(const StagingDirectory&) = delete;
    auto operator=(const StagingDirectory&) -> StagingDirectory& = delete;
    ~StagingDirectory() noexcept;

private:
    std::filesystem::path path;
};

StagingDirectory::StagingDirectory(std::filesystem::path path) noexcept
    : path(std::move(path)) {}

StagingDirectory::~StagingDirectory() noexcept {
    auto ignored = std::error_code();
    std::filesystem::remove_all(path, ignored);
}

} // namespace

namespace graver {

auto collect_inputs(std::span<const std::string_view> inputs) noexcept
    -> std::expected<std::vector<std::filesystem::path>, std::string> {
    auto result = std::vector<std::filesystem::path>();
    auto error = std::error_code();
    for (const auto input : inputs) {
        const auto path = path_from_utf8(input);
        const auto status = std::filesystem::status(path, error);
        if (error || !std::filesystem::exists(status)) {
            return std::unexpected(
                std::format(
                    "cannot read source file '{}': {}",
                    input,
                    error ? error.message() : "not found"
                )
            );
        }
        if (std::filesystem::is_regular_file(status)) {
            result.push_back(path);
            continue;
        }
        if (!std::filesystem::is_directory(status)) {
            return std::unexpected(
                std::format("input is not a regular file or directory: {}", input)
            );
        }
        auto iterator = std::filesystem::recursive_directory_iterator(path, error);
        if (error) {
            return std::unexpected(
                std::format("cannot read directory '{}': {}", input, error.message())
            );
        }
        const auto end = std::filesystem::recursive_directory_iterator();
        while (iterator != end) {
            const auto entry = iterator->symlink_status(error);
            if (error) {
                return std::unexpected(error.message());
            }
            const auto skip = std::filesystem::is_symlink(entry);
            const auto name = path_to_generic_utf8(iterator->path().filename());
            if (std::filesystem::is_directory(entry)
                && (skip || name.starts_with('.') || name == "build")) {
                iterator.disable_recursion_pending();
            } else if (!skip
                       && std::filesystem::is_regular_file(entry)
                       && iterator->path().extension() == ".cv") {
                result.push_back(iterator->path());
            }
            iterator.increment(error);
            if (error) {
                return std::unexpected(
                    std::format("cannot traverse '{}': {}", input, error.message())
                );
            }
        }
    }
    for (auto& path : result) {
        const auto absolute = std::filesystem::absolute(path, error);
        if (error) {
            return std::unexpected(error.message());
        }
        // Removing . is safe, but collapsing .. across a symlink changes its target.
        path.clear();
        for (const auto& component : absolute) {
            if (component != ".") {
                path /= component;
            }
        }
    }
    std::ranges::sort(result);
    auto identities = std::set<std::filesystem::path>();
    auto unique = std::vector<std::filesystem::path>();
    unique.reserve(result.size());
    for (auto& path : result) {
        // Resolve only the parent for identity. Keep the final symlink distinct
        // from its target so write mode can still reject it, even in a mixed batch.
        const auto parent = std::filesystem::canonical(path.parent_path(), error);
        if (error) {
            return std::unexpected(
                std::format(
                    "cannot resolve input '{}': {}",
                    path_to_generic_utf8(path),
                    error.message()
                )
            );
        }
        if (identities.insert(parent / path.filename()).second) {
            unique.push_back(std::move(path));
        }
    }
    return unique;
}

auto replace_file(
    const std::filesystem::path& path,
    std::string_view original,
    std::string_view formatted
) noexcept -> std::expected<void, std::string> {
    auto error = std::error_code();
    const auto status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(status)) {
        return std::unexpected("write requires a regular file, not a symlink or special file");
    }
    auto staging = std::filesystem::path();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (auto attempt = 0; attempt < 128; ++attempt) {
        auto candidate = path.parent_path() / std::format(".graver-{}-{}", stamp, attempt);
        if (std::filesystem::create_directory(candidate, error)) {
            staging = std::move(candidate);
            break;
        }
        if (error && error != std::errc::file_exists) {
            return std::unexpected(std::format("cannot stage formatted file: {}", error.message()));
        }
    }
    if (staging.empty()) {
        return std::unexpected("cannot create a unique staging directory");
    }

    const auto cleanup = StagingDirectory(staging);
    const auto temporary = staging / "formatted";
    const auto written = write_file(temporary, formatted);
    if (!written) {
        return std::unexpected(
            std::format("cannot stage formatted file: {}", written.error().code.message())
        );
    }
    std::filesystem::permissions(temporary, status.permissions(), error);
    if (error) {
        return std::unexpected(
            std::format("cannot preserve file permissions: {}", error.message())
        );
    }
    const auto latest = read_file(path, std::numeric_limits<std::uint32_t>::max());
    if (!latest || *latest != original) {
        return std::unexpected("source changed since it was read; refusing to overwrite it");
    }
    const auto current_status = std::filesystem::symlink_status(path, error);
    if (error || !std::filesystem::is_regular_file(current_status)) {
        return std::unexpected("source is no longer a regular file");
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        return std::unexpected(std::format("cannot replace source file: {}", error.message()));
    }
    return {};
}

}
