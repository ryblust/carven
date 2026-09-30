module carven:semantic.analysis.stage.iteration;

import :semantic.semir.structured;

// Each expanded iteration owns its local storage and pattern bindings. Outer
// storage keeps its identity; the source region keeps its original tables.
auto bind_expanded_iterations(const StructuredBodyDraft& source, SemanticRegion region) noexcept
    -> StructuredRegionDraft;
