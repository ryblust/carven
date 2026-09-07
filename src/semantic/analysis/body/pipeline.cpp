module carven:semantic.analysis.body.pipeline.impl;

import :semantic.analysis.body.context;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.program;

auto elaborate_body_batch(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage
) noexcept -> AnalysisResult<void> {
    return BodyBatchElaborator(draft, catalog, import_usage).run();
}
