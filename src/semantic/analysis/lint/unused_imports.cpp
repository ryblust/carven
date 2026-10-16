module carven:semantic.analysis.lint.unused_imports.impl;

import :diagnostics.builder;
import :semantic.analysis.lint.unused_imports;
import :semantic.analysis.program;
import :source.text;

auto diagnose_unused_imports(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    const ImportUsage& usage
) noexcept -> void {
    for (const auto& module_record : catalog.modules()) {
        if (draft.imported(module_record.module_id)) {
            continue;
        }
        for (const auto& binding : catalog.cpp_imports(module_record.module_id)) {
            if (!binding.opens_namespace
                && !usage.cpp_was_used(module_record.module_id, binding.origin)) {
                draft.diagnostics().warning(
                    DiagnosticBuilder(DiagnosticCode::LintUnusedImport, "unused C++ import")
                        .primary(
                            locate(draft.module_source_id(module_record.module_id), binding.origin)
                        )
                        .build()
                );
            }
        }
    }
    for (const auto& binding : catalog.imports()) {
        if (draft.imported(binding.importer) || usage.was_used(binding.binding_id)) {
            continue;
        }
        draft.diagnostics().warning(
            DiagnosticBuilder(DiagnosticCode::LintUnusedImport, "unused import")
                .primary(locate(draft.module_source_id(binding.importer), binding.declaration_span))
                .build()
        );
    }
}
