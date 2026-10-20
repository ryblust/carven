module carven:semantic.analysis.interop.impl;

import :diagnostics.builder;
import :diagnostics.diagnostic;
import :semantic.analysis.interop;
import :semantic.analysis.program;
import :semantic.semir.decl;
import std;

namespace {

struct CppExportPath final {
    std::vector<std::string> path;
    ProgramOriginID origin;
};

auto is_strict_prefix(
    std::span<const std::string> prefix,
    std::span<const std::string> value
) noexcept -> bool {
    return prefix.size() < value.size() && std::ranges::equal(prefix, value.first(prefix.size()));
}

} // namespace

auto diagnose_cpp_api_surface(ProgramDraft& draft, AnalysisCatalogView catalog) noexcept
    -> AnalysisResult<void> {
    auto failure = std::optional<AnalysisFailure>();
    const auto diagnose = [&](Diagnostic diagnostic) noexcept {
        const auto current = draft.diagnostics().error(std::move(diagnostic));
        if (!failure.has_value()) {
            failure = current;
        }
    };
    auto functions = std::vector<CppExportPath>();
    for (const auto& symbol : catalog.symbols()) {
        const auto* form = std::get_if<CatalogFunctionForm>(&symbol.form);
        if (form == nullptr) {
            continue;
        }
        const auto function = draft.function_declaration_copy(form->function);
        if (!function.cpp_export_origin.has_value()) {
            continue;
        }
        auto path = draft.module_path_copy(symbol.module_id).components()
            | std::views::transform([](std::string_view value) static {
                        return std::string(value);
                    })
            | std::ranges::to<std::vector>();
        path.push_back(draft.spelling_copy(function.name));
        functions.push_back({
            .path = std::move(path),
            .origin = *function.cpp_export_origin,
        });
    }
    for (auto left = 0uz; left < functions.size(); ++left) {
        for (auto right = left + 1; right < functions.size(); ++right) {
            const auto collision = is_strict_prefix(functions[left].path, functions[right].path)
                || is_strict_prefix(functions[right].path, functions[left].path);
            if (!collision) {
                continue;
            }
            diagnose(DiagnosticBuilder(
                         DiagnosticCode::CppAPIPathCollision,
                         "a C++ API function name conflicts with another API namespace path"
            )
                         .primary(draft.source_span(functions[right].origin))
                         .related(
                             draft.source_span(functions[left].origin),
                             "conflicting C++ API declaration"
                         )
                         .build());
        }
    }
    return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                               : AnalysisResult<void>();
}
