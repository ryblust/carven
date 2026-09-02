module carven:artifacts;

import std;

enum class GeneratedArtifactRole {
    Interface,
    CppAPIHeader,
    ModuleImplementation,
    TestEntry,
};

enum class ArtifactSourceMappingPolicy {
    StableInterface,
    SourceAttributed,
};

struct GeneratedArtifact final {
    std::string logical_path;
    GeneratedArtifactRole role;
    ArtifactSourceMappingPolicy source_mapping;
    std::string content;
};

auto validate_artifact_logical_path(std::string_view path) noexcept
    -> std::expected<void, std::string>;

class ArtifactSet final {
public:
    explicit ArtifactSet(std::vector<GeneratedArtifact> artifacts) noexcept;

    ArtifactSet(const ArtifactSet&) = default;
    ArtifactSet(ArtifactSet&&) = default;

    auto operator=(const ArtifactSet&) -> ArtifactSet& = delete;
    auto operator=(ArtifactSet&&) -> ArtifactSet& = delete;

    auto artifacts() const noexcept -> std::span<const GeneratedArtifact>;

private:
    std::vector<GeneratedArtifact> artifacts_;
};
