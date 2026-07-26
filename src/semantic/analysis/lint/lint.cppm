module carven:semantic.analysis.lint;

import :diagnostics.sink;
import :semantic.analysis.builder;
import :semantic.analysis.catalog;
import :source.provenance;

auto diagnose_unused_bindings(
    const SemanticConstruction& construction,
    DiagnosticSink& diagnostics
) noexcept -> void;

auto diagnose_unused_imports(
    AnalysisCatalogView catalog,
    CompilationProvenanceView provenance,
    DiagnosticSink& diagnostics
) noexcept -> void;
