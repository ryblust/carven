module carven:semantic.analysis.types.standard;

import :semantic.analysis.catalog;
import :semantic.analysis.construction.requests;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.type;
import :source.standard;
import :source.text;

auto resolve_standard_record(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ConstructionRequests& requests,
    ProgramModuleID module,
    Span span,
    StandardRecord record
) noexcept -> AnalysisTask<TypeID>;
