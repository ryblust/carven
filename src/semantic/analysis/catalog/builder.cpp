module carven:semantic.analysis.catalog.builder.impl;

import :diagnostics.builder;
import :semantic.analysis.catalog;
import :semantic.analysis.catalog.builder;
import :semantic.analysis.program;
import std;

AnalysisCatalogBuilder::AnalysisCatalogBuilder(
    ProgramDraft& draft,
    CheckedDefinitions* seed
) noexcept
    : draft(draft),
      result(seed ? std::move(*seed->catalog) : AnalysisCatalog()) {
    if (seed) {
        for (auto& symbol : result.symbols) {
            symbol.item_id.reset();
        }
        for (auto& module_decl : result.modules) {
            module_decl.items.clear();
        }
        seed->catalog.reset();
    }
}

auto AnalysisCatalogBuilder::build() && noexcept -> std::expected<AnalysisCatalog, Diagnostics> {
    collect_declarations();
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    bind_imports();
    if (!diagnostics.empty()) {
        return std::unexpected(std::move(diagnostics));
    }
    provide_defaults();
    return std::move(result);
}

auto AnalysisCatalogBuilder::binding_error(SourceID source, std::string message, Span span) noexcept
    -> Diagnostic {
    return DiagnosticBuilder(DiagnosticCode::Catalog, std::move(message))
        .primary(locate(source, span))
        .build();
}

auto build_analysis_catalog(ProgramDraft& draft, CheckedDefinitions* seed) noexcept
    -> std::expected<AnalysisCatalog, Diagnostics> {
    return AnalysisCatalogBuilder(draft, seed).build();
}
