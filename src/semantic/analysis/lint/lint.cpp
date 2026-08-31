module carven:semantic.analysis.lint.impl;

import :diagnostics.builder;
import :semantic.analysis.lint;
import :source.text;

auto diagnose_unused_bindings(SemanticDraftView construction, DiagnosticSink& diagnostics) noexcept
    -> void {
    for (const auto& state : construction.symbol_states()) {
        if (!state.unused_candidate.has_value() || state.symbol.referenced) {
            continue;
        }
        if (state.explicit_capture) {
            diagnostics.emit(
                DiagnosticBuilder(
                    DiagnosticCode::LambdaCaptureUnused,
                    "unused explicit lambda capture"
                )
                    .primary(construction.provenance().source_span(*state.unused_candidate))
                    .build()
            );
            continue;
        }
        const auto parameter = state.role == SemanticSymbolRole::Parameter;
        diagnostics.emit(
            DiagnosticBuilder(
                parameter ? DiagnosticCode::LintUnusedParameter : DiagnosticCode::LintUnusedLocal,
                parameter ? "unused function parameter" : "unused local binding"
            )
                .primary(construction.provenance().source_span(*state.unused_candidate))
                .build()
        );
    }
}

auto diagnose_unused_imports(
    AnalysisCatalogView catalog,
    CompilationProvenanceView provenance,
    DiagnosticSink& diagnostics
) noexcept -> void {
    for (const auto& binding : catalog.imports()) {
        if (binding.used) {
            continue;
        }
        const auto& source =
            provenance.source_snapshot(provenance.module_record(binding.importer).source_id);
        diagnostics.emit(DiagnosticBuilder(DiagnosticCode::LintUnusedImport, "unused import")
                             .primary(locate(source.manager_source_id(), binding.declaration_span))
                             .build());
    }
}
