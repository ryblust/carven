module carven:editor.semantic.impl;

import :compiler.analysis;
import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :editor.document;
import :editor.semantic;
import :semantic.analysis.source;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.text;
import std;

namespace editor {

SemanticAnalysis::SemanticAnalysis(
    SourceManager sources,
    std::map<std::string, SourceID, std::less<>> document_sources,
    std::optional<SemIRProgram> program,
    Diagnostics diagnostics,
    std::vector<SemanticOutput> output,
    std::vector<SourceOccurrence> occurrences
) noexcept
    : source_manager(std::move(sources)),
      source_ids(std::move(document_sources)),
      semantic_program(std::move(program)),
      findings(std::move(diagnostics)),
      execution_output(std::move(output)) {
    for (auto& occurrence : occurrences) {
        source_occurrences[occurrence.location.source_id].push_back(std::move(occurrence));
    }
}

auto SemanticAnalysis::program() const noexcept -> const SemIRProgram* {
    return semantic_program ? std::addressof(*semantic_program) : nullptr;
}

auto SemanticAnalysis::sources() const noexcept -> const SourceManager& {
    return source_manager;
}

auto SemanticAnalysis::diagnostics() const noexcept -> std::span<const Diagnostic> {
    return findings;
}

auto SemanticAnalysis::output() const noexcept -> std::span<const SemanticOutput> {
    return execution_output;
}

auto SemanticAnalysis::select(std::string_view document, std::uint32_t offset) const noexcept
    -> const SourceOccurrence* {
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

auto SemanticAnalysis::locate_source(SourceSpan location) const noexcept
    -> std::optional<DocumentLocation> {
    const auto source = source_manager.try_view(location.source_id);
    if (!source || !try_slice(source->text, location.span)) {
        return std::nullopt;
    }
    return DocumentLocation {.document = std::string(source->origin), .range = location.span};
}

auto SemanticAnalysis::hover(std::string_view document, std::uint32_t offset) const noexcept
    -> std::optional<HoverInformation> {
    const auto* occurrence = select(document, offset);
    if (occurrence == nullptr || !occurrence->type) {
        return std::nullopt;
    }
    const auto location = locate_source(occurrence->location);
    return location
        ? std::optional(HoverInformation {.location = *location, .type = *occurrence->type})
        : std::nullopt;
}

auto SemanticAnalysis::definition(std::string_view document, std::uint32_t offset) const noexcept
    -> std::optional<DocumentLocation> {
    const auto* occurrence = select(document, offset);
    return occurrence == nullptr || !occurrence->definition
        ? std::nullopt
        : locate_source(*occurrence->definition);
}

auto SemanticAnalysis::references(std::string_view document, std::uint32_t offset) const noexcept
    -> std::optional<std::vector<DocumentLocation>> {
    const auto* selected = select(document, offset);
    if (selected == nullptr || !selected->definition) {
        return std::nullopt;
    }
    auto result = std::vector<DocumentLocation>();
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

auto SemanticAnalysis::source(std::string_view document) const noexcept
    -> std::optional<SourceView> {
    const auto found = source_ids.find(document);
    if (found == source_ids.end()) {
        return std::nullopt;
    }
    return source_manager.view(found->second);
}

auto analyze_project(std::span<const SemanticInput> inputs) noexcept
    -> std::shared_ptr<const SemanticAnalysis> {
    auto sources = SourceManager();
    auto source_ids = std::map<std::string, SourceID, std::less<>>();
    auto modules = std::vector<SourceModuleInput>();
    auto diagnostics = Diagnostics();
    auto output = std::vector<SemanticOutput>();
    auto program = std::optional<SemIRProgram>();
    auto occurrences = std::vector<SourceOccurrence>();
    modules.reserve(inputs.size());
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
            const auto id = sources.append_virtual(input.module.document, std::string(source.text));
            if (!id) {
                diagnostics.push_back(DiagnosticBuilder(
                                          DiagnosticCode::CompilationInput,
                                          std::format(
                                              "cannot load project document '{}': {}",
                                              input.module.document,
                                              id.error().message
                                          )
                )
                                          .build());
                continue;
            }
            found = source_ids.emplace(input.module.document, *id).first;
        }
        // Preserve repeated mappings for the compiler's input validation rather
        // than silently collapsing two modules onto the same source snapshot.
        modules.push_back({.source_id = found->second, .module_path = input.module.module_path});
    }
    if (diagnostics.empty()) {
        const auto capture = [&](ExecutionOutputStream stream, std::string_view bytes) noexcept {
            output.push_back({.stream = stream, .bytes = std::string(bytes)});
        };
        const auto collect = [&](std::span<const SourceOccurrence> facts) noexcept {
            occurrences.assign(facts.begin(), facts.end());
        };
        auto analyzed =
            analyze_compilation(sources, SourceBatch {.modules = modules}, capture, {}, collect);
        if (analyzed) {
            program.emplace(std::move(analyzed->value));
            diagnostics = std::move(analyzed->diagnostics);
        } else {
            diagnostics = std::move(analyzed.error());
        }
    }
    return std::shared_ptr<const SemanticAnalysis>(new SemanticAnalysis(
        std::move(sources),
        std::move(source_ids),
        std::move(program),
        std::move(diagnostics),
        std::move(output),
        std::move(occurrences)
    ));
}

} // namespace editor
