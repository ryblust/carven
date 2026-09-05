module carven:semantic.analysis.lint.unused_imports.impl;

import :diagnostics.builder;
import :semantic.analysis.lint.unused_imports;
import :source.text;

auto diagnose_unused_imports(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    const ImportUsage& usage
) noexcept -> void {
    for (const auto& binding : catalog.imports()) {
        if (usage.was_used(binding.binding_id)) {
            continue;
        }
        draft.diagnostics().warning(
            DiagnosticBuilder(DiagnosticCode::LintUnusedImport, "unused import")
                .primary(locate(
                    draft.syntax_tree(binding.importer).view().source_id(),
                    binding.declaration_span
                ))
                .build()
        );
    }
}
