#include <carven/std/fs/native.hpp>

#include <carven/runtime/text/utf8.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace carven::standard::fs {

namespace {

auto path_from_utf8(std::string_view value) noexcept -> std::filesystem::path {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}

auto io_error() noexcept -> std::int32_t {
    return errno == 0 ? EIO : errno;
}

auto resolve_path(std::string_view path, bool require_existing) noexcept -> PathResult {
    if (path.find('\0') != std::string_view::npos) {
        return {.code = EINVAL, .path = {}};
    }
    auto error = std::error_code();
    const auto input = std::filesystem::absolute(path_from_utf8(path), error);
    if (error) {
        return {.code = error.value(), .path = {}};
    }
    const auto resolved = require_existing ? std::filesystem::canonical(input, error)
                                           : std::filesystem::weakly_canonical(input, error);
    if (error) {
        return {.code = error.value(), .path = {}};
    }
    const auto encoded = resolved.generic_u8string();
    const auto value =
        std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size());
    if (!runtime::utf8_is_valid(value)) {
        return {.code = EILSEQ, .path = {}};
    }
    return {.code = 0, .path = runtime::String::from_str(value)};
}

} // namespace

auto ReadResult::take_bytes() noexcept -> runtime::Vector<std::uint8_t> {
    return std::move(bytes);
}

auto PathResult::take_path() noexcept -> runtime::String {
    return std::move(path);
}

auto PathsResult::take_paths() noexcept -> runtime::Vector<runtime::String> {
    return std::move(paths);
}

auto canonical(std::string_view path) noexcept -> PathResult {
    return resolve_path(path, true);
}

auto resolve(std::string_view path) noexcept -> PathResult {
    return resolve_path(path, false);
}

auto parent(std::string_view path) noexcept -> runtime::String {
    const auto encoded = path_from_utf8(path).parent_path().generic_u8string();
    return runtime::String::from_str(
        std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size())
    );
}

auto join(std::string_view base, std::string_view path) noexcept -> runtime::String {
    const auto encoded = (path_from_utf8(base) / path_from_utf8(path)).generic_u8string();
    return runtime::String::from_str(
        std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size())
    );
}

auto remove(std::string_view path) noexcept -> RemovalResult {
    if (path.find('\0') != std::string_view::npos) {
        return {.code = EINVAL, .removed = false};
    }
    auto error = std::error_code();
    const auto removed = std::filesystem::remove(path_from_utf8(path), error);
    return {.code = error.value(), .removed = removed};
}

auto files(std::string_view root, bool recursive, std::string_view excluded_root) noexcept
    -> PathsResult {
    if (root.find('\0') != std::string_view::npos
        || excluded_root.find('\0') != std::string_view::npos) {
        return {.code = EINVAL, .paths = {}};
    }
    auto error = std::error_code();
    const auto path = path_from_utf8(root);
    if (!std::filesystem::exists(path, error)) {
        return {.code = error.value(), .paths = {}};
    }
    auto excluded = std::filesystem::path();
    if (!excluded_root.empty()) {
        excluded = std::filesystem::canonical(path_from_utf8(excluded_root), error);
        if (error) {
            return {.code = error.value(), .paths = {}};
        }
    }
    const auto is_excluded = [&](const std::filesystem::path& candidate) noexcept {
        if (excluded.empty()) {
            return false;
        }
        const auto identity = std::filesystem::canonical(candidate, error);
        return !error
            && std::mismatch(excluded.begin(), excluded.end(), identity.begin(), identity.end())
                   .first
            == excluded.end();
    };
    if (is_excluded(path)) {
        return {.code = 0, .paths = {}};
    }
    if (error) {
        return {.code = error.value(), .paths = {}};
    }
    auto iterator = std::filesystem::recursive_directory_iterator(path, error);
    auto paths = runtime::Vector<runtime::String>();
    while (!error && iterator != std::filesystem::recursive_directory_iterator()) {
        if (!recursive) {
            iterator.disable_recursion_pending();
        }
        if (is_excluded(iterator->path())) {
            if (iterator->is_directory(error)) {
                iterator.disable_recursion_pending();
            }
        } else if (!error && iterator->is_regular_file(error)) {
            const auto encoded = iterator->path().generic_u8string();
            const auto entry =
                std::string_view(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            if (!runtime::utf8_is_valid(entry)) {
                return {.code = EILSEQ, .paths = {}};
            }
            paths.push(runtime::String::from_str(entry));
        }
        if (!error) {
            iterator.increment(error);
        }
    }
    std::ranges::sort(paths, {}, [](const auto& path) noexcept { return path.as_str(); });
    return {.code = error.value(), .paths = std::move(paths)};
}

auto read_bytes(std::string_view path) noexcept -> ReadResult {
    if (path.find('\0') != std::string_view::npos) {
        return {.bytes = {}, .code = EINVAL};
    }
    errno = 0;
    auto file = std::ifstream(path_from_utf8(path), std::ios::binary);
    if (!file) {
        return {.bytes = {}, .code = io_error()};
    }
    auto bytes = runtime::Vector<std::uint8_t>();
    auto buffer = std::array<char, 16384>();
    while (file) {
        errno = 0;
        file.read(buffer.data(), buffer.size());
        if (file.bad() || (file.fail() && !file.eof())) {
            return {.bytes = {}, .code = io_error()};
        }
        for (auto index = std::streamsize(0); index < file.gcount(); ++index) {
            bytes.push(static_cast<std::uint8_t>(buffer[index]));
        }
    }
    return {.bytes = std::move(bytes), .code = 0};
}

auto write_bytes(std::string_view path, runtime::Slice<std::uint8_t> bytes) noexcept
    -> std::int32_t {
    if (path.find('\0') != std::string_view::npos) {
        return EINVAL;
    }
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max())) {
        return EFBIG;
    }
    errno = 0;
    auto file = std::ofstream(path_from_utf8(path), std::ios::binary | std::ios::trunc);
    if (!file) {
        return io_error();
    }
    if (!bytes.empty()) {
        file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        );
    }
    file.close();
    return file.fail() ? io_error() : 0;
}

auto status(std::string_view path) noexcept -> StatusResult {
    if (path.find('\0') != std::string_view::npos) {
        return {.code = EINVAL, .kind = 0};
    }
    auto error = std::error_code();
    const auto value = std::filesystem::status(path_from_utf8(path), error);
    if (error == std::errc::no_such_file_or_directory) {
        return {.code = 0, .kind = 0};
    }
    if (error) {
        return {.code = error.value(), .kind = 0};
    }
    return {
        .code = 0,
        .kind = !std::filesystem::exists(value)       ? 0
            : std::filesystem::is_regular_file(value) ? 1
            : std::filesystem::is_directory(value)    ? 2
                                                      : 3
    };
}

auto create_directories(std::string_view path) noexcept -> std::int32_t {
    if (path.find('\0') != std::string_view::npos) {
        return EINVAL;
    }
    auto error = std::error_code();
    std::filesystem::create_directories(path_from_utf8(path), error);
    return error.value();
}

} // namespace carven::standard::fs
