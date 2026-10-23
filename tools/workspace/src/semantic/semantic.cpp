module;

#include <carven/std/providers.hpp>

module carven:workspace.semantic.impl;

import :compiler.analysis;
import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.document;
import :frontend.program.parse;
import :semantic.analysis.source;
import :semantic.evaluation.execution;
import :semantic.evaluation.native;
import :semantic.evaluation.output;
import :semantic.semir.program;
import :source.batch;
import :source.document;
import :source.identifier;
import :source.manager;
import :source.text;
import :support.invariant;
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

auto WorkspaceSemanticAnalysis::source_construction_complete() const noexcept -> bool {
    return analysis.source_construction_complete();
}

auto WorkspaceSemanticAnalysis::has_source_analysis() const noexcept -> bool {
    return analysis.has_source_analysis();
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

auto WorkspaceSemanticAnalysis::call_target(std::string_view document, Span callee) const noexcept
    -> std::optional<WorkspaceDocumentLocation> {
    for (const auto& occurrence : occurrences(document)) {
        if (occurrence.location.span == callee
            && occurrence.callable
            && occurrence.callable->target) {
            return locate_source(*occurrence.callable->target);
        }
    }
    return std::nullopt;
}

auto WorkspaceSemanticAnalysis::hover(
    std::string_view document,
    std::uint32_t offset
) const noexcept -> std::optional<WorkspaceHoverInformation> {
    const auto* occurrence = select(document, offset);
    if (occurrence == nullptr
        || (!occurrence->type && !occurrence->entity && occurrence->type_description.empty())) {
        return std::nullopt;
    }
    const auto location = locate_source(occurrence->location);
    return location ? std::optional(
                          WorkspaceHoverInformation {
                              .location = *location,
                              .type = occurrence->type,
                              .entity = occurrence->entity,
                              .type_description = occurrence->type_description,
                              .callable = occurrence->callable,
                              .description = occurrence->type_description,
                              .documentation = occurrence->documentation
                          }
                      )
                    : std::nullopt;
}

auto WorkspaceSemanticAnalysis::definition(
    std::string_view document,
    std::uint32_t offset
) const noexcept -> std::optional<WorkspaceDocumentLocation> {
    const auto* occurrence = select(document, offset);
    return occurrence == nullptr || !occurrence->definition()
        ? std::nullopt
        : locate_source(*occurrence->definition());
}

auto WorkspaceSemanticAnalysis::references(
    std::string_view document,
    std::uint32_t offset,
    bool include_declaration
) const noexcept -> std::optional<std::vector<WorkspaceDocumentLocation>> {
    const auto* selected = select(document, offset);
    if (selected == nullptr || !selected->entity) {
        return std::nullopt;
    }
    auto result = std::vector<WorkspaceDocumentLocation>();
    for (const auto& [source_id, occurrences] : source_occurrences) {
        for (const auto& occurrence : occurrences) {
            if (occurrence.entity != selected->entity
                || (!include_declaration && occurrence.definition() == occurrence.location)) {
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

auto WorkspaceSemanticAnalysis::completion_context() const noexcept -> SourceCompletionContext {
    return analysis.completion_context();
}

auto WorkspaceSemanticAnalysis::completions() const noexcept
    -> std::span<const SourceCompletionCandidate> {
    return query_completions.empty()
        ? analysis.source_completions()
        : std::span<const SourceCompletionCandidate>(query_completions);
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
    bool source_query,
    TimingOutput timings,
    std::optional<WorkspaceDocumentLocation> completion
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
            diagnostics.push_back(DiagnosticBuilder(
                                      DiagnosticCode::CompilationInput,
                                      std::format(
                                          "project document '{}' is not available",
                                          input.project_module.document
                                      )
            )
                                      .build());
            continue;
        }
        auto found = source_ids.find(input.project_module.document);
        if (found == source_ids.end()) {
            const auto source = input.source->source();
            const auto id = sources.append_shared(input.source->sources(), source.source_id);
            found = source_ids.emplace(input.project_module.document, id).first;
            if (input.parsed) {
                parsed.push_back({.source_id = id, .document = input.parsed});
            }
        }
        // Repeated mappings are diagnosed by the compiler, not collapsed here.
        modules.push_back(
            {.source_id = found->second, .module_path = input.project_module.module_path}
        );
    }
    auto excluded = std::vector<SourceSpan>();
    for (const auto& input : inputs) {
        if (const auto found = source_ids.find(input.project_module.document);
            found != source_ids.end()) {
            for (const auto range : input.excluded) {
                excluded.push_back(locate(found->second, range));
            }
        }
    }
    auto position = std::optional<SourceSpan>();
    if (completion) {
        const auto found = source_ids.find(completion->document);
        if (found != source_ids.end()) {
            position = locate(found->second, completion->range);
        }
    }
    preparation_scope.stop();
    static const auto native_bindings = []() noexcept {
        auto bindings = std::vector<ExecutionNativeBinding>();
        carven::standard::visit_native_bindings(
            [&]<auto Function>(std::string_view name, bool environment_effects) noexcept {
                bindings.push_back(
                    execution_native_function<Function>(
                        std::string(name),
                        {.static_execution = true, .environment_effects = environment_effects}
                    )
                );
            },
            [&]<typename From, typename To>(std::string_view name) noexcept {
                bindings.push_back(
                    execution_native_conversion<From, To>(
                        std::string(name),
                        {.static_execution = true, .environment_effects = false}
                    )
                );
            }
        );
        return bindings;
    }();
    auto native = ExecutionNativeHost(native_bindings, static_execution_limits());
    auto analyzed = diagnostics.empty()
        ? analyze_compilation(
              std::move(sources),
              SourceBatch {.modules = modules},
              {.output = {},
               .timings = timings,
               .collect_source_observations = true,
               .capture_output = true,
               .host_effects = HostEffects::Deny,
               .completion = position,
               .source_query = source_query,
               .excluded = excluded,
               .native_call = native},
              parsed
          )
        : CompilationAnalysis::failed(std::move(sources), std::move(diagnostics));
    const auto index_scope = TimingScope(timings, TimingStage::SourceIndex);
    return std::shared_ptr<const WorkspaceSemanticAnalysis>(
        new WorkspaceSemanticAnalysis(std::move(analyzed), std::move(source_ids))
    );
}

auto WorkspaceSemanticAnalysis::alias_references(
    std::string_view document,
    std::uint32_t offset
) const noexcept -> std::optional<std::vector<WorkspaceDocumentLocation>> {
    const auto* selected = select(document, offset);
    if (selected == nullptr || !selected->alias) {
        return std::nullopt;
    }
    auto result = std::vector<WorkspaceDocumentLocation>();
    for (const auto& [source_id, occurrences] : source_occurrences) {
        for (const auto& occurrence : occurrences) {
            if (occurrence.alias == selected->alias) {
                if (auto location = locate_source(occurrence.location)) {
                    result.push_back(std::move(*location));
                }
            }
        }
    }
    return result;
}

auto WorkspaceSemanticAnalysis::prepare_rename(
    std::string_view document,
    std::uint32_t offset
) const noexcept -> std::optional<WorkspaceRenameInformation> {
    const auto* selected = select(document, offset);
    if (!selected || !selected->entity) {
        return std::nullopt;
    }
    const auto target = selected->definition();
    const auto declaration = target ? locate_source(*target) : std::nullopt;
    const auto location = locate_source(selected->location);
    if (!declaration || !location) {
        return std::nullopt;
    }
    const auto text = slice(source(document)->text, location->range);
    if (!std::holds_alternative<OrdinaryIdentifier>(classify_identifier(text)) || text == "ptr") {
        return std::nullopt;
    }
    auto references = std::vector<WorkspaceDocumentLocation>();
    for (const auto& [id, facts] : source_occurrences) {
        for (const auto& fact : facts) {
            const auto matches = selected->alias ? fact.alias == selected->alias
                                                 : fact.entity == selected->entity && !fact.alias;
            if (matches) {
                if (const auto located = locate_source(fact.location)) {
                    references.push_back(*located);
                }
            }
        }
    }
    std::ranges::sort(references, [](const auto& a, const auto& b) {
        return std::pair(a.document, a.range) < std::pair(b.document, b.range);
    });
    references.erase(
        std::unique(
            references.begin(),
            references.end(),
            [](const auto& a, const auto& b) {
                return a.document == b.document && a.range == b.range;
            }
        ),
        references.end()
    );
    return WorkspaceRenameInformation {
        *location,
        *declaration,
        std::string(text),
        std::move(references)
    };
}

auto WorkspaceSemanticAnalysis::preserves_bindings(
    const WorkspaceSemanticAnalysis& before,
    FunctionRef<Span(std::string_view, Span) noexcept> remap
) const noexcept -> bool {
    const auto rebind = [&](SourceSpan location) -> std::optional<SourceSpan> {
        const auto original = before.sources().view(location.source_id);
        const auto target = source(original.origin);
        if (!target) {
            return std::nullopt;
        }
        return locate(target->source_id, remap(original.origin, location.span));
    };
    for (const auto& [id, facts] : before.source_occurrences) {
        const auto document = before.sources().view(id).origin;
        for (const auto& fact : facts) {
            if (!fact.entity) {
                continue;
            }
            const auto location = remap(document, fact.location.span);
            const auto* selected = select(document, location.start());
            if (!selected || !selected->entity) {
                return false;
            }
            auto entity = *fact.entity;
            if (const auto* declared = std::get_if<SourceSpan>(&entity)) {
                const auto mapped = rebind(*declared);
                if (!mapped) {
                    return false;
                }
                entity = *mapped;
            }
            const auto alias = fact.alias ? rebind(*fact.alias) : std::nullopt;
            if (selected->entity != entity || selected->alias != alias) {
                return false;
            }
        }
    }
    return true;
}

auto WorkspaceSemanticAnalysis::occurrences(std::string_view document) const noexcept
    -> std::span<const SourceOccurrence> {
    const auto input = source(document);
    if (!input) {
        return {};
    }
    const auto found = source_occurrences.find(input->source_id);
    return found == source_occurrences.end() ? std::span<const SourceOccurrence>() : found->second;
}
