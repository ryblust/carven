module carven:semantic.analysis.lint;

import :diagnostics.sink;
import :semantic.analysis.session;
import :semantic.analysis.catalog;
import :semantic.analysis.session.read;
import :source.provenance;

auto diagnose_unused_bindings(SemanticDraftView construction, DiagnosticSink& diagnostics) noexcept
    -> void;

auto diagnose_unused_imports(
    AnalysisCatalogView catalog,
    CompilationProvenanceView provenance,
    DiagnosticSink& diagnostics
) noexcept -> void;
