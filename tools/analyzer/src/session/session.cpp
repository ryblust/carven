module carven:analyzer.session.impl;

import :analyzer.session;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :semantic.analysis.types.display;
import :semantic.evaluation.output;
import :semantic.semir.ids;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.text;
import :support.invariant;
import :support.visit;
import :workspace.analysis;
import :workspace.semantic;
import std;

namespace {
auto failure(std::string code, std::string message) noexcept -> AnalyzerResponse {
    return {
        .document_versions = {},
        .result = AnalyzerFailure {.code = std::move(code), .message = std::move(message)}
    };
}

auto locate(const WorkspaceSemanticQuery& query, SourceSpan span) noexcept
    -> WorkspaceVersionedLocation {
    const auto source = query.result->sources().try_view(span.source_id);
    if (!source || !try_slice(source->text, span.span)) {
        invariant_violation("analyzer diagnostic span has no selected source");
    }
    const auto found =
        std::ranges::find(query.documents, source->origin, &WorkspaceDocumentVersion::document);
    if (found == query.documents.end()) {
        invariant_violation("analyzer diagnostic source has no document version");
    }
    return WorkspaceVersionedLocation {
        .document = found->document,
        .version = found->version,
        .range = span.span
    };
}

auto label(const WorkspaceSemanticQuery& query, const DiagnosticLabel& value) noexcept
    -> AnalyzerDiagnosticLabel {
    return {.location = locate(query, value.span), .message = value.message};
}

auto findings(const WorkspaceSemanticQuery& query) noexcept -> AnalyzerCheckResult {
    auto result = AnalyzerCheckResult {
        .published = query.result->program() != nullptr,
        .diagnostics = {},
        .output = {}
    };
    for (const auto& diagnostic : query.result->diagnostics()) {
        auto value = AnalyzerDiagnostic {
            .severity =
                diagnostic.finding.severity == DiagnosticSeverity::Error ? "error" : "warning",
            .code = std::string(diagnostic_code_info(diagnostic.finding.code).name),
            .message = diagnostic.finding.message,
            .primary = {},
            .related = {},
            .notes = {},
            .helps = diagnostic.attachment.helps
        };
        if (diagnostic.attachment.primary) {
            value.primary = label(query, *diagnostic.attachment.primary);
        }
        for (const auto& related : diagnostic.attachment.related) {
            value.related.push_back(label(query, related));
        }
        for (const auto& note : diagnostic.attachment.notes) {
            value.notes.push_back(
                {.location = note.span ? std::optional(locate(query, *note.span)) : std::nullopt,
                 .message = note.message}
            );
        }
        result.diagnostics.push_back(std::move(value));
    }
    for (const auto& output : query.result->output()) {
        result.output.push_back(
            {.stream = output.stream == ExecutionOutputStream::Standard ? "stdout" : "stderr",
             .bytes = output.bytes}
        );
    }
    return result;
}
} // namespace

auto AnalyzerSession::execute(AnalyzerRequest request) noexcept -> AnalyzerResponse {
    return request.visit(
        Overloaded {
            [&](AnalyzerUpdate& value) noexcept -> AnalyzerResponse {
                const auto version = value.version;
                const auto result =
                    host.update(std::move(value.document), version, std::move(value.text));
                if (!result) {
                    if (const auto* stale =
                            std::get_if<WorkspaceStaleDocumentVersion>(&result.error())) {
                        return failure(
                            "stale_version",
                            std::format(
                                "received version {}; current version is {}",
                                stale->received,
                                stale->current
                            )
                        );
                    }
                    return failure(
                        "source_input",
                        std::get<SourceLoadError>(result.error()).message
                    );
                }
                return {.document_versions = {}, .result = AnalyzerAcknowledgement {}};
            },
            [&](const AnalyzerClose& value) noexcept -> AnalyzerResponse {
                host.remove(value.document);
                return {.document_versions = {}, .result = AnalyzerAcknowledgement {}};
            },
            [&](const AnalyzerReplaceProject& value) noexcept -> AnalyzerResponse {
                auto selected = std::vector<WorkspaceProjectModule>();
                for (const auto& input : value.modules) {
                    auto path = CanonicalModulePath::from_value(input.module_path);
                    if (!path) {
                        return failure(
                            "module_path",
                            std::format("invalid module path '{}'", input.module_path)
                        );
                    }
                    selected.push_back(
                        {.document = input.document, .module_path = std::move(*path)}
                    );
                }
                project = std::move(selected);
                return {.document_versions = {}, .result = AnalyzerAcknowledgement {}};
            },
            [&](AnalyzerCheck) noexcept -> AnalyzerResponse {
                auto query = host.snapshot().semantic(project);
                auto result = findings(query);
                return {
                    .document_versions = std::move(query.documents),
                    .result = std::move(result)
                };
            },
            [&](const AnalyzerHover& value) noexcept -> AnalyzerResponse {
                auto query = host.snapshot().hover(project, value.document, value.offset);
                auto information = std::optional<AnalyzerHoverInformation>();
                if (query.result) {
                    const auto found = std::ranges::find(
                        query.analysis.documents,
                        query.result->location.document,
                        &WorkspaceDocumentVersion::document
                    );
                    if (found == query.analysis.documents.end()) {
                        invariant_violation("analyzer hover source has no document version");
                    }
                    auto text = query.result->type.visit(
                        Overloaded {
                            [](BuiltinType type) static noexcept {
                                return type_display_name(type);
                            },
                            [&](TypeID type) noexcept {
                                return type_display_name(*query.analysis.result->program(), type);
                            }
                        }
                    );
                    information = AnalyzerHoverInformation {
                        .location =
                            {.document = found->document,
                             .version = found->version,
                             .range = query.result->location.range},
                        .type_text = std::move(text)
                    };
                }
                return {
                    .document_versions = std::move(query.analysis.documents),
                    .result = AnalyzerHoverResult {.information = std::move(information)}
                };
            },
            [&](const AnalyzerDefinition& value) noexcept -> AnalyzerResponse {
                auto query = host.snapshot().definition(project, value.document, value.offset);
                return {
                    .document_versions = std::move(query.analysis.documents),
                    .result = AnalyzerDefinitionResult {.location = std::move(query.result)}
                };
            },
            [&](const AnalyzerReferences& value) noexcept -> AnalyzerResponse {
                auto query = host.snapshot().references(project, value.document, value.offset);
                return {
                    .document_versions = std::move(query.analysis.documents),
                    .result = AnalyzerReferencesResult {.locations = std::move(query.result)}
                };
            },
            [](AnalyzerStop) static noexcept -> AnalyzerResponse {
                return {.document_versions = {}, .result = AnalyzerAcknowledgement {}};
            }
        }
    );
}

auto AnalyzerSession::counts() const noexcept -> WorkspaceQueryCounts {
    return host.snapshot().counts();
}
