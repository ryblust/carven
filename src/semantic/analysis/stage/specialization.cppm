module carven:semantic.analysis.stage.specialization;

import :semantic.analysis.diagnostics;
import :semantic.analysis.stage.session;
import :semantic.semir.structured;
import std;

using StaticEnvironment = std::map<LocalBindingID, ConstantID>;

// Rewrites a copy of a checked region into executable form for one static
// environment: static bindings become constants, a const if becomes its
// selected arm, a const for becomes one copy per index, a call with static
// arguments names its instance, and const blocks execute and disappear.
// Returns whether the region changed.
auto specialize_region(
    StaticStage& stage,
    BodyID body,
    StaticEnvironment environment,
    SemanticRegion& region
) noexcept -> AnalysisTask<bool>;
