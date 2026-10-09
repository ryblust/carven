module carven:workspace.semantic.impl;

import :compiler.analysis;
import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.document;
import :frontend.program.parse;
import :semantic.analysis.source;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.document;
import :source.manager;
import :source.text;
import :support.timing;
import :workspace.semantic;
import std;

WorkspaceSemanticAnalysis::WorkspaceSemanticAnalysis(
    CompilationAnalysis result,
    std::map<std::string, SourceID, std::less<>> document_sources
) noexcept
    : analysis(std::move(result)),
      source_ids(std::move(document_sources)) {
    for (const auto& occurrence : analysis.source_occurrences()) {
        source_occurrences[occurrence.location.source_id].push_back(occurrence);
    }
}

auto WorkspaceSemanticAnalysis::program() const noexcept -> const SemIRProgram* {
    return analysis.program();
}

auto WorkspaceSemanticAnalysis::sources() const noexcept -> const SourceManager& {
    return analysis.sources();
}

auto WorkspaceSemanticAnalysis::diagnostics() const noexcept -> std::span<const Diagnostic> {
    return analysis.diagnostics();
}

auto WorkspaceSemanticAnalysis::output() const noexcept -> std::span<const CompilationOutput> {
    return analysis.output();
}

auto WorkspaceSemanticAnalysis::select(
    std::string_view document,
    std::uint32_t offset
) const noexcept -> const SourceOccurrence* {
    const auto input = source(document);
    if (!input || offset >= input->text.size()) {
        return nullptr;
    }
    const auto found = source_occurrences.find(input->source_id);
    if (found == source_occurrences.end()) {
        return nullptr;
    }
    const auto* selected = static_cast<const SourceOccurrence*>(nullptr);
    for (const auto& occurrence : found->second) {
        const auto span = occurrence.location.span;
        if (offset < span.start() || offset >= span.end()) {
            continue;
        }
        // Select the source occurrence before inspecting its evidence. An
        // unresolved name must mask the type of an enclosing expression.
        if (selected == nullptr || span.size() < selected->location.span.size()) {
            selected = std::addressof(occurrence);
        }
    }
    return selected;
}

auto WorkspaceSemanticAnalysis::locate_source(SourceSpan location) const noexcept
    -> std::optional<WorkspaceDocumentLocation> {
    const auto source = sources().try_view(location.source_id);
    if (!source || !try_slice(source->text, location.span)) {
        return std::nullopt;
    }
    return WorkspaceDocumentLocation {
        .document = std::string(source->origin),
        .range = location.span
    };
}

auto WorkspaceSemanticAnalysis::hover(
    std::string_view document,
    std::uint32_t offset
) const noexcept -> std::optional<WorkspaceHoverInformation> {
    const auto* occurrence = select(document, offset);
    if (occurrence == nullptr || !occurrence->type) {
        return std::nullopt;
    }
    const auto location = locate_source(occurrence->location);
    return location
        ? std::optional(
              WorkspaceHoverInformation {.location = *location, .type = *occurrence->type}
          )
        : std::nullopt;
}

auto WorkspaceSemanticAnalysis::definition(
    std::string_view document,
    std::uint32_t offset
) const noexcept -> std::optional<WorkspaceDocumentLocation> {
    const auto* occurrence = select(document, offset);
    return occurrence == nullptr || !occurrence->definition
        ? std::nullopt
        : locate_source(*occurrence->definition);
}

auto WorkspaceSemanticAnalysis::references(
    std::string_view document,
    std::uint32_t offset
) const noexcept -> std::optional<std::vector<WorkspaceDocumentLocation>> {
    const auto* selected = select(document, offset);
    if (selected == nullptr || !selected->definition) {
        return std::nullopt;
    }
    auto result = std::vector<WorkspaceDocumentLocation>();
    const auto target = *selected->definition;
    for (const auto& [source_id, occurrences] : source_occurrences) {
        for (const auto& occurrence : occurrences) {
            if (!occurrence.definition
                || occurrence.definition->source_id != target.source_id
                || occurrence.definition->span != target.span) {
                continue;
            }
            if (auto location = locate_source(occurrence.location)) {
                result.push_back(std::move(*location));
            }
        }
    }
    std::ranges::sort(result, [](const auto& left, const auto& right) static noexcept {
        return std::pair(std::string_view(left.document), left.range)
            < std::pair(std::string_view(right.document), right.range);
    });
    result.erase(
        std::unique(
            result.begin(),
            result.end(),
            [](const auto& left, const auto& right) static noexcept {
                return left.document == right.document && left.range == right.range;
            }
        ),
        result.end()
    );
    return result;
}

auto WorkspaceSemanticAnalysis::source(std::string_view document) const noexcept
    -> std::optional<SourceView> {
    const auto found = source_ids.find(document);
    if (found == source_ids.end()) {
        return std::nullopt;
    }
    return sources().view(found->second);
}

auto analyze_workspace_project(
    std::span<const WorkspaceSemanticInput> inputs,
    TimingOutput timings
) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis> {
    auto preparation_scope = TimingScope(timings, TimingStage::SourceLoading);
    auto sources = SourceManager();
    auto source_ids = std::map<std::string, SourceID, std::less<>>();
    auto modules = std::vector<SourceModuleInput>();
    auto parsed = std::vector<ParsedSourceInput>();
    auto diagnostics = Diagnostics();
    modules.reserve(inputs.size());
    parsed.reserve(inputs.size());
    for (const auto& input : inputs) {
        if (!input.source) {
            diagnostics.push_back(
                DiagnosticBuilder(
                    DiagnosticCode::CompilationInput,
                    std::format("project document '{}' is not available", input.module.document)
                )
                    .build()
            );
            continue;
        }
        auto found = source_ids.find(input.module.document);
        if (found == source_ids.end()) {
            const auto source = input.source->source();
            const auto id = sources.append_shared(input.source->sources(), source.source_id);
            found = source_ids.emplace(input.module.document, id).first;
            if (input.parsed) {
                parsed.push_back({.source_id = id, .document = input.parsed});
            }
        }
        // Repeated mappings are diagnosed by the compiler, not collapsed here.
        modules.push_back({.source_id = found->second, .module_path = input.module.module_path});
    }
    preparation_scope.stop();
    auto analyzed = diagnostics.empty()
        ? analyze_compilation(
              std::move(sources),
              SourceBatch {.modules = modules},
              {.output = {},
               .timings = timings,
               .collect_source_observations = true,
               .capture_output = true},
              parsed
          )
        : CompilationAnalysis::failed(std::move(sources), std::move(diagnostics));
    const auto index_scope = TimingScope(timings, TimingStage::SourceIndex);
    return std::shared_ptr<const WorkspaceSemanticAnalysis>(
        new WorkspaceSemanticAnalysis(std::move(analyzed), std::move(source_ids))
    );
}
