module carven:artifacts.materialize;

import :artifacts;
import std;

[[nodiscard]] auto write_artifacts(
    const std::filesystem::path& output_root,
    const GeneratedArtifactSet& artifacts
) noexcept -> std::expected<void, std::string>;
