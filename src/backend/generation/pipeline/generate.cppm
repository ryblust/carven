module carven:backend.generate;

import :artifacts;
import :backend.generation.request;
import :semantic.semir.program;
import :source.module_path;
import std;

auto generate_artifacts(
    SemIRProgram semantic,
    const TargetPlanningRequest& request,
    std::optional<std::span<const CanonicalModulePath>> selected_modules = std::nullopt
) noexcept -> GeneratedArtifactSet;
