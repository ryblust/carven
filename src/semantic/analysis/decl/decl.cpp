module carven:semantic.analysis.decl.impl;

import :semantic.analysis.decl.context;
import :semantic.analysis.decl;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import :semantic.analysis.program;
import std;

using decl_resolution::resolve_declarations;
using decl_resolution::validate_declaration_surfaces;

auto complete_declarations(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage
) noexcept -> AnalysisResult<void> {
    auto resolved = resolve_declarations(draft, catalog, import_usage);
    if (!resolved.has_value()) {
        return std::unexpected(resolved.error());
    }

    auto failure = std::optional<AnalysisFailure>();
    const auto retain_first_failure = [&](AnalysisResult<void> result) noexcept {
        if (!result.has_value() && !failure.has_value()) {
            failure = result.error();
        }
    };
    retain_first_failure(validate_declaration_surfaces(draft, catalog));
    retain_first_failure(analyze_nominal_containment(draft));
    retain_first_failure(diagnose_cpp_api_surface(draft, catalog));
    return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                               : AnalysisResult<void>();
}
