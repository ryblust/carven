module carven:semantic.analysis.body.resolve;

import :semantic.analysis.diagnostics;
import :semantic.analysis.failure;
import :semantic.semir.constant;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.provenance;
import std;

auto resolve_body(
    StructuredBodyDraft body,
    const TypeResolution& types,
    const CanonicalTypeStore& canonical_types,
    const ConstantStore& constants,
    const std::vector<bool>& test_stops,
    const FailureSolution& failures,
    const FailureSetStore& failure_sets,
    CompilationProvenanceReader provenance,
    AnalysisDiagnostics diagnostics
) noexcept -> SemIRBody;
