module carven:semantic.analysis.body.pipeline;

import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.semir.body;
import :semantic.semir.program;
import std;

auto elaborate_body_batch(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage
) noexcept -> AnalysisResult<void>;
