module carven:semantic.analysis.catalog.builder.impl;

import :diagnostics.builder;
import :semantic.analysis.catalog;
import :semantic.analysis.catalog.builder;
import :semantic.analysis.program;
import std;

AnalysisCatalogBuilder::AnalysisCatalogBuilder(ProgramDraft& draft) noexcept
    : draft(draft) {}

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

auto build_analysis_catalog(ProgramDraft& draft) noexcept
    -> std::expected<AnalysisCatalog, Diagnostics> {
    return AnalysisCatalogBuilder(draft).build();
}
