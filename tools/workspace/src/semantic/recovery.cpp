module carven:workspace.semantic.recovery.impl;

import :compiler.analysis;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.ast.stmt;
import :frontend.document;
import :semantic.analysis.source;
import :source.text;
import :workspace.semantic;
import std;

auto analyze_workspace_source_queries(
    std::span<const WorkspaceSemanticInput> inputs,
    const std::optional<WorkspaceDocumentLocation>& completion,
    std::shared_ptr<const WorkspaceSemanticAnalysis> initial
) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis> {
    // Only an unpublished semantic observation can seed this detached index.
    // Completion changes the input text; frontend failures have no semantic facts.
    if (initial && (completion || !initial->has_source_analysis() || initial->program())) {
        initial.reset();
    }
    auto query_inputs = std::vector<WorkspaceSemanticInput>(inputs.begin(), inputs.end());
    auto steps = std::vector<std::shared_ptr<const WorkspaceSemanticAnalysis>>();
    while (true) {
        // A failed ordinary construction already contains detached partial facts.
        // Frontend failures start observation from the retained recovered syntax.
        const auto result = initial ? std::exchange(initial, {})
                                    : analyze_workspace_project(query_inputs, true, {}, completion);
        steps.push_back(result);
        // Validation errors do not leave syntax to recover. All source facts
        // already exist, with publication identities detached by the compiler.
        if (result->source_construction_complete()
            || (completion && !result->completions().empty())) {
            break;
        }
        auto changed = false;
        for (const auto& diagnostic : result->diagnostics()) {
            if (diagnostic.finding.severity != DiagnosticSeverity::Error
                || !diagnostic.attachment.primary
                || diagnostic.finding.code == DiagnosticCode::FlowMissingReturn) {
                continue;
            }
            const auto location = diagnostic.attachment.primary->span;
            const auto source = result->sources().try_view(location.source_id);
            if (!source) {
                continue;
            }
            const auto found = std::ranges::find_if(query_inputs, [&](const auto& input) {
                return input.project_module.document == source->origin;
            });
            if (found == query_inputs.end() || !found->parsed) {
                continue;
            }
            const auto syntax = found->parsed->recovered_syntax();
            if (!syntax) {
                continue;
            }
            auto selected = std::optional<Span>();
            const auto consider = [&](Span range) {
                if (range.start() <= location.span.start()
                    && location.span.start() < range.end()
                    && location.span.end() <= range.end()
                    && (!selected || range.size() < selected->size())) {
                    selected = range;
                }
            };
            for (const auto& statement : syntax->statements()) {
                consider(statement.span);
            }
            if (!selected || std::ranges::contains(found->excluded, *selected)) {
                continue;
            }
            found->excluded.push_back(*selected);
            changed = true;
        }
        if (!changed) {
            break;
        }
        // Exclusions are compiler body-observation inputs. The immutable syntax
        // and its import/declaration scope are shared by every pass.
    }
    // Completion owns detached candidates and needs no merged navigation index.
    if (completion) {
        return steps.back();
    }
    const auto& last = *steps.back();
    auto result = std::shared_ptr<WorkspaceSemanticAnalysis>(new WorkspaceSemanticAnalysis(
        CompilationAnalysis::failed(
            last.sources().snapshot(),
            std::vector<Diagnostic>(last.diagnostics().begin(), last.diagnostics().end())
        ),
        last.source_ids
    ));
    result->query_completions.assign(last.completions().begin(), last.completions().end());
    // Detached evidence from a failed expression is useful too: for example the
    // selected callee in an initializer whose result has the wrong type.
    auto occurrences = std::map<std::pair<SourceID, Span>, SourceOccurrence>();
    for (const auto& step : steps) {
        const auto rebind = [&](SourceSpan value) -> SourceSpan {
            const auto original = step->sources().view(value.source_id);
            return locate(result->source_ids.at(std::string(original.origin)), value.span);
        };
        for (const auto& [source, facts] : step->source_occurrences) {
            for (auto fact : facts) {
                fact.location = rebind(fact.location);
                if (fact.callable && fact.callable->target) {
                    fact.callable->target = rebind(*fact.callable->target);
                }
                if (fact.external_binding) {
                    fact.external_binding = rebind(*fact.external_binding);
                }
                if (fact.alias) {
                    fact.alias = rebind(*fact.alias);
                }
                if (fact.entity) {
                    if (auto* location = std::get_if<SourceSpan>(&*fact.entity)) {
                        *location = rebind(*location);
                    }
                }
                const auto key = std::pair(fact.location.source_id, fact.location.span);
                const auto [entry, inserted] = occurrences.try_emplace(key, fact);
                if (!inserted) {
                    auto& previous = entry->second;
                    if (fact.type) {
                        previous.type = fact.type;
                    }
                    if (!fact.type_description.empty()) {
                        previous.type_description = std::move(fact.type_description);
                    }
                    if (fact.callable) {
                        previous.callable = std::move(fact.callable);
                    }
                    if (!fact.documentation.empty()) {
                        previous.documentation = std::move(fact.documentation);
                    }
                    if (fact.entity) {
                        previous.entity = fact.entity;
                    }
                    if (fact.alias) {
                        previous.alias = fact.alias;
                    }
                    if (fact.external_binding) {
                        previous.external_binding = fact.external_binding;
                    }
                }
            }
        }
    }
    for (auto& [key, fact] : occurrences) {
        result->source_occurrences[key.first].push_back(std::move(fact));
    }
    return result;
}
