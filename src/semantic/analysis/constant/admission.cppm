module carven:semantic.analysis.constant.admission;

import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.ids;
import std;

// A const fn promises structural compile-time executability for every
// semantically reachable path. Reachable function dependencies must also
// explicitly promise that capability.
auto validate_const_contracts(
    ProgramDraft& draft,
    std::span<const std::optional<BodyID>> function_bodies
) noexcept -> AnalysisResult<void>;
