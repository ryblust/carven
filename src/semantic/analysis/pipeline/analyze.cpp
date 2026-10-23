module carven:semantic.analyze.impl;

import :diagnostics.builder;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :semantic.analysis.catalog;
import :semantic.analysis.construction;
import :semantic.analysis.lint.unused_imports;
import :semantic.analysis.program;
import :semantic.analysis.program.checked;
import :semantic.analysis.source;
import :semantic.analysis.source.builder;
import :semantic.analyze;
import :semantic.artifact;
import :semantic.evaluation.execution;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :support.timing;
import std;

auto analyze(
    SyntaxProgram syntax,
    HostEffects host_effects,
    ExecutionOutput output,
    TimingOutput timings,
    SourceAnalysisOutput source_output,
    AnalysisNativeCall native_call
) noexcept -> std::expected<Diagnosed<SemIRProgram>, Diagnostics> {
    auto result = analyze_checked(
        std::move(syntax),
        host_effects,
        false,
        false,
        std::nullopt,
        output,
        timings,
        source_output,
        {},
        native_call
    );
    if (!result) {
        return std::unexpected(std::move(result.error()));
    }
    return Diagnosed<SemIRProgram> {
        .value = std::move(result->value.program),
        .diagnostics = std::move(result->diagnostics),
    };
}

auto analyze_checked(
    SyntaxProgram syntax,
    HostEffects host_effects,
    bool retain_definitions,
    bool source_query,
    std::optional<CheckedDefinitions> seed,
    ExecutionOutput output,
    TimingOutput timings,
    SourceAnalysisOutput source_output,
    SourceCompletionRequest completion,
    AnalysisNativeCall native_call
) noexcept -> std::expected<Diagnosed<CheckedUnit>, Diagnostics> {
    const auto scope = TimingScope(timings, TimingStage::SemanticAnalysis);
    auto observations = std::optional<SourceAnalysisBuilder>();
    if (source_output || completion.output) {
        observations.emplace(completion, source_query);
    }
    auto result = [&]() noexcept -> std::expected<Diagnosed<CheckedUnit>, Diagnostics> {
        auto diagnostics = DiagnosticSink();
        auto draft = ProgramDraft::begin(
            std::move(syntax),
            diagnostics,
            host_effects,
            output,
            observations ? std::addressof(*observations) : nullptr,
            timings,
            seed ? std::addressof(*seed) : nullptr,
            native_call
        );

        auto definitions = std::vector<std::byte>();
        {
            auto catalog_scope = TimingScope(timings, TimingStage::SemanticCatalog);
            auto catalog_result =
                build_analysis_catalog(draft, seed ? std::addressof(*seed) : nullptr);
            catalog_scope.stop();
            if (!catalog_result.has_value()) {
                return std::unexpected(std::move(catalog_result.error()));
            }
            const auto catalog = std::move(*catalog_result);
            auto import_usage = ImportUsage(catalog.view().imports().size());

            const auto construction =
                ProgramConstruction(draft, catalog.view(), import_usage).run();
            if (observations) {
                observations->describe(draft, catalog.view());
            }
            if (!construction.has_value() || diagnostics.has_errors()) {
                return std::unexpected(diagnostics.take());
            }
            diagnose_unused_imports(draft, catalog.view(), import_usage);
            if (diagnostics.has_errors()) {
                return std::unexpected(diagnostics.take());
            }
            if (retain_definitions) {
                auto snapshot = capture_checked_definitions(draft, catalog);
                if (!snapshot) {
                    diagnostics.emit(DiagnosticBuilder(
                                         DiagnosticCode::CompilationInput,
                                         std::format(
                                             "cannot retain checked semantic definitions: {}",
                                             snapshot.error().message
                                         )
                    )
                                         .build());
                    return std::unexpected(diagnostics.take());
                }
                definitions = std::move(*snapshot);
            }
        }
        const auto cacheable = draft.cacheable();
        auto published = std::move(draft).finish();
        if (!published.has_value() || diagnostics.has_errors()) {
            return std::unexpected(diagnostics.take());
        }
        return Diagnosed<CheckedUnit> {
            .value =
                CheckedUnit {
                    .program = std::move(*published),
                    .definitions = std::move(definitions),
                    .cacheable = cacheable,
                },
            .diagnostics = diagnostics.take(),
        };
    }();
    if (observations) {
        const auto source_scope = TimingScope(timings, TimingStage::SourceObservations);
        const auto construction_complete = observations->construction_complete();
        const auto occurrences =
            std::move(*observations).finish(result.has_value() && !source_query);
        if (source_output) {
            source_output(
                {.occurrences = occurrences, .construction_complete = construction_complete}
            );
        }
    }
    return result;
}
