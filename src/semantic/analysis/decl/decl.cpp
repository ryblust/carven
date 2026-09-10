module carven:semantic.analysis.decl.impl;

import :semantic.analysis.decl.context;
import :semantic.analysis.decl;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import :semantic.analysis.program;
import std;

auto resolve_declaration_heads(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage
) noexcept -> AnalysisResult<void> {
    auto resolved = resolve_declarations(draft, catalog, import_usage);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    static_cast<void>(analyze_nominal_containment(draft));
    static_cast<void>(diagnose_cpp_api_surface(draft, catalog));
    if (const auto failure = draft.diagnostics().failure()) {
        return std::unexpected(*failure);
    }
    return {};
}
