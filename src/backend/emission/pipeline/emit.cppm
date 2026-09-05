module carven:backend.emit;

import :artifacts;
import :backend.emission.render;
import :backend.target;
import std;

auto emit(
    TargetUnit unit,
    std::string logical_path,
    GeneratedArtifactRole role,
    EmissionPolicy policy
) noexcept -> GeneratedArtifact;
