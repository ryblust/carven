module carven:semantic.analysis.names;

import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.program;
import :source.text;
import std;

auto lookup_cpp_name(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& usage,
    ProgramModuleID source_module,
    CppNameLookup lookup,
    std::span<const Span> components
) noexcept -> AnalysisResult<std::optional<CppNameReference>>;
