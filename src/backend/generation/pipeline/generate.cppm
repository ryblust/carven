module carven:backend.generate;

import :artifacts;
import :backend.generation.request;
import :semantic.semir;
import std;

auto generate_artifacts(SemIRProgram semantic, const TargetPlanningRequest& request) noexcept
    -> GeneratedArtifactSet;
