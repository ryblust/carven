module carven:semantic.analysis.pipeline.declarations;

import :semantic.analysis.analyzer;
import :semantic.analysis.catalog;

auto collect_declarations(AnalysisCatalogView catalog, ProgramAnalyzer& draft) noexcept -> void;
