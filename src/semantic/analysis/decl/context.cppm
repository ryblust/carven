module carven:semantic.analysis.decl.context;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.catalog;
import :semantic.analysis.decl;
import :semantic.analysis.program;
import :semantic.semir.program;
import :source.text;
import :support.invariant;
import std;

auto require_catalog_symbol(AnalysisCatalogView catalog, CatalogSymbolID id) noexcept
    -> const CatalogSymbol& {
    const auto* result = catalog.symbol(id);
    if (result == nullptr) {
        invariant_violation("declaration resolution referenced an unknown catalog symbol");
    }
    return *result;
}

auto declaration_source_id(const ProgramDraft& draft, ProgramModuleID module_id) noexcept
    -> SourceID {
    return draft.syntax_tree(module_id).view().source_id();
}

auto declaration_source_origin(ProgramDraft& draft, ProgramModuleID module_id, Span span) noexcept
    -> ProgramOriginID {
    return draft.append_source_origin(draft.module_source(module_id), span);
}

auto declaration_failure(
    const ProgramDraft& draft,
    ProgramModuleID module_id,
    Span span,
    DiagnosticCode code,
    std::string message
) noexcept -> AnalysisFailure {
    return draft.diagnostics().error(
        DiagnosticBuilder(code, std::move(message))
            .primary(locate(declaration_source_id(draft, module_id), span))
            .build()
    );
}

auto resolve_declarations(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage
) noexcept -> AnalysisResult<void>;

auto validate_declaration_surfaces(ProgramDraft& draft, AnalysisCatalogView catalog) noexcept
    -> AnalysisResult<void>;
