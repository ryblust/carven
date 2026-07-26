module carven:backend.generate;

import :artifacts;
import :compilation.request;
import :semantic.hir;
import std;

auto generate_target(const SemanticProgram& semantic, TargetGenerationRequest request) noexcept
    -> ArtifactSet;
