module carven:semantic.analysis.lint.unused_imports;

import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.semir.program;

auto diagnose_unused_imports(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    const ImportUsage& usage
) noexcept -> void;
