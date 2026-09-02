module carven:backend.generate;

import :artifacts;
import :backend.generation.request;
import :semantic.hir;
import std;

auto generate_artifacts(SemanticProgram semantic, TargetGenerationRequest request) noexcept
    -> ArtifactSet;
