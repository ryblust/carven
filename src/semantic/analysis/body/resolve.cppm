module carven:semantic.analysis.body.resolve;

import :semantic.analysis.diagnostics;
import :semantic.analysis.failure;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.provenance;

auto resolve_body(
    StructuredBodyDraft body,
    const TypeResolution& types,
    const FailureSolution& failures,
    const FailureSetStore& failure_sets,
    CompilationProvenanceReader provenance,
    AnalysisDiagnostics diagnostics
) noexcept -> SemIRBody;
