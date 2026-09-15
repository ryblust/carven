module carven:semantic.analyze.impl;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :semantic.analysis.catalog;
import :semantic.analysis.construction;
import :semantic.analysis.lint.unused_imports;
import :semantic.analysis.program;
import :semantic.analyze;
import :semantic.evaluation.output;
import :semantic.semir.program;
import std;

auto analyze(SyntaxProgram syntax, const ExecutionOutput& output) noexcept
    -> std::expected<Diagnosed<SemIRProgram>, Diagnostics> {
    auto diagnostics = DiagnosticSink();
    auto draft = ProgramDraft::begin(std::move(syntax), diagnostics, output);

    {
        auto catalog_result = build_analysis_catalog(draft);
        if (!catalog_result.has_value()) {
            return std::unexpected(std::move(catalog_result.error()));
        }
        const auto catalog = std::move(*catalog_result);
        auto import_usage = ImportUsage(catalog.view().imports().size());

        const auto construction = ProgramConstruction(draft, catalog.view(), import_usage).run();
        if (!construction.has_value() || diagnostics.has_errors()) {
            return std::unexpected(diagnostics.take());
        }
        diagnose_unused_imports(draft, catalog.view(), import_usage);
        if (diagnostics.has_errors()) {
            return std::unexpected(diagnostics.take());
        }
    }
    auto published = std::move(draft).finish();
    if (!published.has_value() || diagnostics.has_errors()) {
        return std::unexpected(diagnostics.take());
    }
    return Diagnosed<SemIRProgram> {
        .value = std::move(*published),
        .diagnostics = diagnostics.take(),
    };
}
