module carven:semantic.analysis.decl;

import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.program;

auto resolve_declaration_heads(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage
) noexcept -> AnalysisResult<void>;
