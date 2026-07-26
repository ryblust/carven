module carven:artifacts;

import std;

struct GeneratedArtifact final {
    std::string logical_path;
    std::string content;
};

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
