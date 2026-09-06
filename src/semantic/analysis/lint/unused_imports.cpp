module carven:semantic.analysis.lint.unused_imports.impl;

import :diagnostics.builder;
import :semantic.analysis.lint.unused_imports;
import :source.text;

auto diagnose_unused_imports(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    const ImportUsage& usage
) noexcept -> void {
    for (const auto& module_record : catalog.modules()) {
        for (const auto& binding : catalog.cpp_imports(module_record.module_id)) {
            if (!binding.opens_namespace
                && !usage.cpp_was_used(module_record.module_id, binding.origin)) {
                draft.diagnostics().warning(
                    DiagnosticBuilder(DiagnosticCode::LintUnusedImport, "unused C++ import")
                        .primary(locate(
                            draft.syntax_tree(module_record.module_id).view().source_id(),
                            binding.origin
                        ))
                        .build()
                );
            }
        }
    }
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
