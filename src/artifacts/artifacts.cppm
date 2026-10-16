module carven:artifacts;

import std;

enum class GeneratedArtifactRole {
    Interface,
    CppAPIHeader,
    ModuleImplementation,
    TestRunnerHeader,
    TestEntry,
    SemanticIR,
};

enum class ArtifactSourceMappingPolicy {
    StableInterface,
    SourceAttributed,
};

struct GeneratedArtifact final {
    std::string logical_path;
    GeneratedArtifactRole role;
    ArtifactSourceMappingPolicy source_mapping;
    // The source that owns a generated implementation; synthetic artifacts have none.
    std::optional<std::string> source_path;
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
    auto take_entries() && noexcept -> std::vector<GeneratedArtifact>;

private:
    std::vector<GeneratedArtifact> artifacts;
};
