module carven:semantic.analysis.body.pipeline.impl;

import :semantic.analysis.body.context;
import :semantic.analysis.body.pipeline;

using body_elaboration::BatchElaborator;

auto elaborate_body_batch(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage
) noexcept -> AnalysisResult<void> {
    return BatchElaborator(draft, catalog, import_usage).run();
}
