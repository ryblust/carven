module carven:artifacts.materialize.impl;

import :artifacts;
import :artifacts.materialize;
import :support.file;
import std;

namespace {

auto establish_directory(const std::filesystem::path& path) noexcept
    -> std::expected<void, std::string> {
    auto error = std::error_code();
    std::filesystem::create_directories(path, error);
    if (error) {
        return std::unexpected(
            std::format("cannot create directory '{}': {}", path.generic_string(), error.message())
        );
    }
    return {};
}

} // namespace

auto write_artifacts(
    const std::filesystem::path& output_root,
    const GeneratedArtifactSet& artifacts
) noexcept -> std::expected<void, std::string> {
    if (const auto created = establish_directory(output_root); !created) {
        return std::unexpected(created.error());
    }
    for (const auto& artifact : artifacts.artifacts()) {
        const auto destination = output_root / std::filesystem::path(artifact.logical_path);
        if (const auto created = establish_directory(destination.parent_path()); !created) {
            return std::unexpected(created.error());
        }
        if (const auto written = write_file(destination, artifact.content); !written) {
            return std::unexpected(
                std::format(
                    "cannot write '{}': {}",
                    destination.generic_string(),
                    written.error().code.message()
                )
            );
        }
    }
    return {};
}
