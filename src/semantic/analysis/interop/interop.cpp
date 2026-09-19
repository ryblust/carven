module carven:semantic.analysis.interop.impl;

import :diagnostics.builder;
import :diagnostics.diagnostic;
import :semantic.analysis.interop;
import :semantic.analysis.program;
import :semantic.semir.decl;
import :source.cpp.identifier;
import std;

namespace {

auto is_unrepresentable_cpp_provider_name(std::string_view name) noexcept -> bool {
    return name == "main" || name == "std" || name == "carven";
}

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

auto source_id(const ProgramDraft& draft, ProgramModuleID module_id) noexcept -> SourceID {
    return draft.syntax_tree(module_id).view().source_id();
}

} // namespace

auto validate_cpp_provider(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    const ASTFunctionDecl& function
) noexcept -> AnalysisResult<void> {
    if (!std::holds_alternative<ASTCppImportForm>(function.implementation)) {
        return {};
    }

    auto failure = std::optional<AnalysisFailure>();
    const auto diagnose = [&](Span span, std::string message, DiagnosticCode code) noexcept {
        failure = draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                                .primary(locate(source_id(draft, module_id), span))
                                                .build());
    };
    const auto name = draft.source_slice_copy(module_id, function.name_span);
    if (!is_supported_cpp_identifier(name)) {
        diagnose(
            function.name_span,
            "C++ provider name must be a supported C++ identifier",
            DiagnosticCode::CppIdentifier
        );
    } else if (is_unrepresentable_cpp_provider_name(name)) {
        diagnose(
            function.name_span,
            "this name cannot be represented as a global C++ provider",
            DiagnosticCode::CppIdentifier
        );
    }
    return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                               : AnalysisResult<void>();
}

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
            | std::views::transform([](std::string_view value) { return std::string(value); })
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
