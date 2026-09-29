module carven:test.harness.directory.impl;

import :support.path;
import :test.harness.directory;
import :test.harness.framework;
import std;

namespace carven::testing {

TempDirectory::TempDirectory(std::string_view label) noexcept {
    auto error = std::error_code();
    const auto root = std::filesystem::temp_directory_path(error);
    require(!error).note("temporary directory root: ", error.message());

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (auto attempt = 0u; attempt < 128u; ++attempt) {
        const auto candidate = root / std::format("{}-{}-{}", label, stamp, attempt);
        error.clear();
        if (std::filesystem::create_directory(candidate, error)) {
            directory = candidate;
            return;
        }
        if (error && error != std::errc::file_exists) {
            require(false).note(
                "create temporary directory ",
                path_to_generic_utf8(candidate),
                ": ",
                error.message()
            );
        }
    }
    require(false).note(
        "could not create a unique temporary directory under ",
        path_to_generic_utf8(root)
    );
}

TempDirectory::~TempDirectory() noexcept {
    if (!directory.empty()) {
        auto ignored = std::error_code();
        std::filesystem::remove_all(directory, ignored);
    }
}

auto TempDirectory::path(std::string_view name) const noexcept -> std::filesystem::path {
    return name.empty() ? directory : directory / path_from_utf8(name);
}

} // namespace carven::testing
