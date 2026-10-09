module carven:backend.generate;

import :artifacts;
import :backend.generation.request;
import :semantic.semir.program;
import :source.module_path;
import :support.timing;
import std;

// A source library is supplied as an independently compilable closed batch.
// Invalid library selections are rejected before artifact emission.
auto generate_artifacts(
    SemIRProgram semantic,
    const TargetPlanningRequest& request,
    std::optional<std::span<const CanonicalModulePath>> selected_modules = std::nullopt,
    TimingOutput timings = {}
) noexcept -> std::expected<GeneratedArtifactSet, std::string>;
