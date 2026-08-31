module carven:artifacts.impl;

import :artifacts;
import :support.invariant;
import std;

namespace {

auto check_artifact_logical_path(std::string_view path) noexcept
    -> std::expected<void, std::string> {
    constexpr auto description = std::string_view("artifact logical path");
    if (path.empty()) {
        return std::unexpected(std::format("{} is empty", description));
    }

    if (path.front() == '/' || path.front() == '\\' || path.back() == '/' || path.back() == '\\') {
        return std::unexpected(std::format("{} is not a normalized relative path", description));
    }

    if (path.contains('\\')) {
        return std::unexpected(std::format("{} must use forward slashes", description));
    }

    const auto starts_with_drive = path.size() >= 2
        && ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z'))
        && path[1] == ':';

    if (starts_with_drive) {
        return std::unexpected(std::format("{} is not a normalized relative path", description));
    }

    auto offset = 0uz;
    while (offset < path.size()) {
        const auto separator = path.find('/', offset);
        const auto end = separator == std::string_view::npos ? path.size() : separator;
        const auto component = path.substr(offset, end - offset);

        if (component.empty() || component == "." || component == "..") {
            return std::unexpected(
                std::format("{} contains an empty, '.' or '..' component", description)
            );
        }

        if (std::ranges::any_of(component, [](char value) static noexcept {
                const auto byte = static_cast<unsigned char>(value);
                return byte < 0x20u || byte == 0x7fu;
            })) {
            return std::unexpected(std::format("{} contains a control character", description));
        }

        if (separator == std::string_view::npos) {
            break;
        }

        offset = separator + 1;
    }

    const auto filesystem_path = std::filesystem::path(path);
    if (filesystem_path.is_absolute()
        || filesystem_path.has_root_name()
        || filesystem_path.generic_string() != path) {
        return std::unexpected(std::format("{} is not a normalized relative path", description));
    }
    return {};
}

} // namespace

auto validate_artifact_logical_path(std::string_view path) noexcept
    -> std::expected<void, std::string> {
    return check_artifact_logical_path(path);
}

ArtifactSet::ArtifactSet(std::vector<GeneratedArtifact> artifacts) noexcept
    : artifacts_(std::move(artifacts)) {
    std::ranges::sort(artifacts_, {}, &GeneratedArtifact::logical_path);
    auto logical_paths = std::flat_set<std::string_view> {};
    for (const auto& artifact : artifacts_) {
        if (!validate_artifact_logical_path(artifact.logical_path)) {
            invariant_violation("artifact has an invalid logical path");
        }
        if (logical_paths.contains(artifact.logical_path)) {
            invariant_violation("artifact logical paths are not unique");
        }
        for (auto separator = artifact.logical_path.find('/'); separator != std::string::npos;
             separator = artifact.logical_path.find('/', separator + 1)) {
            if (logical_paths.contains(
                    std::string_view(artifact.logical_path).substr(0, separator)
                )) {
                invariant_violation("artifact path descends from an artifact file");
            }
        }
        logical_paths.insert(artifact.logical_path);
    }
}

auto ArtifactSet::artifacts() const noexcept -> std::span<const GeneratedArtifact> {
    return artifacts_;
}
