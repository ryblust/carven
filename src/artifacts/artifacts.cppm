module carven:artifacts;

import std;

enum class GeneratedArtifactRole {
    Interface,
    CppAPIHeader,
    ModuleImplementation,
    TestRunnerHeader,
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

class GeneratedArtifactSet final {
public:
    explicit GeneratedArtifactSet(std::vector<GeneratedArtifact> values) noexcept;

    GeneratedArtifactSet(const GeneratedArtifactSet&) = default;
    GeneratedArtifactSet(GeneratedArtifactSet&&) = default;

    auto operator=(const GeneratedArtifactSet&) -> GeneratedArtifactSet& = delete;
    auto operator=(GeneratedArtifactSet&&) -> GeneratedArtifactSet& = delete;

    auto entries() const noexcept -> std::span<const GeneratedArtifact>;

private:
    std::vector<GeneratedArtifact> artifacts;
};
