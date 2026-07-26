module carven:support.file.impl;

import :support.file;
import std;

auto read_file(const std::filesystem::path& path, std::size_t max_bytes) noexcept
    -> std::expected<std::string, FileError> {
    auto inspect_error = std::error_code();
    const auto initial_size = std::filesystem::file_size(path, inspect_error);
    if (inspect_error) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Inspect,
                .code = inspect_error,
            }
        );
    }
    if (initial_size > max_bytes) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Inspect,
                .code = std::make_error_code(std::errc::file_too_large),
            }
        );
    }
    const auto initial_write_time = std::filesystem::last_write_time(path, inspect_error);
    if (inspect_error) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Inspect,
                .code = inspect_error,
            }
        );
    }

    auto file = std::ifstream(path, std::ios::binary);
    if (!file.is_open()) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Open,
                .code = std::make_error_code(std::io_errc::stream),
            }
        );
    }

    constexpr auto maximum_stream_size =
        static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max());
    if (initial_size > maximum_stream_size) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Inspect,
                .code = std::make_error_code(std::errc::file_too_large),
            }
        );
    }

    const auto expected_size = static_cast<std::size_t>(initial_size);
    auto content = std::string(expected_size, '\0');
    if (expected_size != 0) {
        file.read(content.data(), static_cast<std::streamsize>(expected_size));
        if (static_cast<std::size_t>(file.gcount()) != expected_size) {
            return std::unexpected(
                FileError {
                    .operation = FileOperation::Read,
                    .code = file.eof()
                        ? std::make_error_code(std::errc::resource_unavailable_try_again)
                        : std::make_error_code(std::io_errc::stream),
                }
            );
        }
    }

    auto extra = char();
    file.read(&extra, 1);
    if (file.gcount() != 0) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Read,
                .code = std::make_error_code(std::errc::resource_unavailable_try_again),
            }
        );
    }
    if (!file.eof()) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Read,
                .code = std::make_error_code(std::io_errc::stream),
            }
        );
    }
    const auto final_size = std::filesystem::file_size(path, inspect_error);
    if (inspect_error) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Inspect,
                .code = inspect_error,
            }
        );
    }
    const auto final_write_time = std::filesystem::last_write_time(path, inspect_error);
    if (inspect_error) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Inspect,
                .code = inspect_error,
            }
        );
    }
    if (final_size != initial_size || final_write_time != initial_write_time) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Read,
                .code = std::make_error_code(std::errc::resource_unavailable_try_again),
            }
        );
    }
    return content;
}

auto write_file(const std::filesystem::path& path, std::string_view content) noexcept
    -> std::expected<void, FileError> {
    auto file = std::ofstream(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Open,
                .code = std::make_error_code(std::io_errc::stream),
            }
        );
    }

    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good()) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Write,
                .code = std::make_error_code(std::io_errc::stream),
            }
        );
    }
    file.flush();
    if (!file.good()) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Flush,
                .code = std::make_error_code(std::io_errc::stream),
            }
        );
    }
    file.close();
    if (file.fail()) {
        return std::unexpected(
            FileError {
                .operation = FileOperation::Close,
                .code = std::make_error_code(std::io_errc::stream),
            }
        );
    }
    return {};
}
