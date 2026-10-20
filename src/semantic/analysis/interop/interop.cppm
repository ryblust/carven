module carven:semantic.analysis.interop;

import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.program;
import std;

auto diagnose_cpp_api_surface(ProgramDraft& draft, AnalysisCatalogView catalog) noexcept
    -> AnalysisResult<void>;
