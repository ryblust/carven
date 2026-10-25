module;

#if defined(_WIN32)
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

module carven:analyzer.main.impl;

import :analyzer.documents;
import :analyzer.project;
import :analyzer.protocol;
import :diagnostics.code;
import :driver.sources;
import :frontend.document;
import :frontend.program.dependencies;
import :source.module_path;
import :source.text;
import :support.path;
import :workspace.analysis;
import :workspace.decorations;
import :workspace.folding;
import :workspace.semantic;
import :workspace.symbols;
import std;

namespace {

constexpr auto message_limit = 16uz * 1024uz * 1024uz;

auto error(std::string_view message) noexcept -> std::unexpected<AnalyzerFailure> {
    return std::unexpected(AnalyzerFailure(std::string(message)));
}

auto range(Span value) noexcept -> AnalyzerRange {
    return {.start = value.start(), .end = value.end()};
}

auto document_symbol(const WorkspaceDocumentSymbol& symbol) noexcept -> AnalyzerSymbol {
    auto result = AnalyzerSymbol {
        .name = symbol.name,
        .kind = symbol.kind,
        .uri = {},
        .range = range(symbol.range),
        .selection = range(symbol.selection),
        .children = {}
    };
    for (const auto& child : symbol.children) {
        result.children.push_back(document_symbol(child));
    }
    return result;
}

auto matches_symbol(std::string_view name, std::string_view query) noexcept -> bool {
    const auto lower = [](unsigned char byte) static noexcept {
        return byte >= 'A' && byte <= 'Z' ? byte + ('a' - 'A') : byte;
    };
    return !std::ranges::search(
                name,
                query,
                [&](char a, char b) noexcept {
                    return lower(static_cast<unsigned char>(a))
                        == lower(static_cast<unsigned char>(b));
                }
           ).empty()
        || query.empty();
}

class Analyzer final {
public:
    auto initialize() noexcept -> std::optional<std::string>;
    auto execute(const AnalyzerRequest& request) noexcept -> AnalyzerResult;
    auto current_revision() const noexcept -> std::uint64_t;

    auto client_document_uris(const AnalyzerRequest& request) const noexcept
        -> std::map<std::string, std::string, std::less<>>;

private:
    WorkspaceAnalysisHost host {16uz};
    std::map<std::string, WorkspaceProjectModule, std::less<>> modules;

    AnalyzerProject model {.sources = {}, .targets = {}, .warnings = {}};
    AnalyzerDocuments documents;
    std::vector<AnalyzerProjectInput> project_models;
    std::vector<std::string> workspace_roots;
    std::string preferred_target;
    std::int64_t source_version = 1;
    std::uint64_t revision = 0;
    auto target_for(std::string_view document) const noexcept -> const AnalyzerProjectTarget*;
    auto disk_source(std::string_view document) noexcept -> AnalyzerProjectSource*;

    auto project(std::string_view document = {}) const noexcept
        -> std::vector<WorkspaceProjectModule>;

    auto synchronize() noexcept -> void;
    auto publish_source(std::string_view document, std::string_view context) noexcept -> void;
    auto prepare_context(std::string_view context) noexcept -> void;

    auto resolve_target(AnalyzerProjectTarget& target) noexcept
        -> std::vector<WorkspaceProjectModule>;

    auto diagnostics() noexcept -> AnalyzerWorkspace;
    auto symbol_documents() const noexcept -> std::set<std::string, std::less<>>;
    auto watchers() const noexcept -> std::vector<AnalyzerWatch>;
};

auto Analyzer::current_revision() const noexcept -> std::uint64_t {
    return revision;
}

auto Analyzer::client_document_uris(const AnalyzerRequest& request) const noexcept
    -> std::map<std::string, std::string, std::less<>> {
    if (request.method == AnalyzerMethod::Workspace
        || request.method == AnalyzerMethod::Update
        || request.method == AnalyzerMethod::Close) {
        return {};
    }
    return documents.client_uris(request.context.empty() ? request.uri : request.context);
}

auto Analyzer::initialize() noexcept -> std::optional<std::string> {
    model = load_analyzer_project({});
    if (!model.warnings.empty()) {
        return model.warnings.front();
    }
    synchronize();
    return std::nullopt;
}

auto Analyzer::target_for(std::string_view document) const noexcept
    -> const AnalyzerProjectTarget* {
    const AnalyzerProjectTarget* selected = nullptr;
    for (const auto& target : model.targets) {
        if (!preferred_target.empty() && target.name != preferred_target) {
            continue;
        }
        if (std::ranges::find(target.documents, document) == target.documents.end()) {
            continue;
        }
        if (!selected || (!selected->default_target && target.default_target)) {
            selected = &target;
        }
    }
    return selected;
}

auto Analyzer::disk_source(std::string_view document) noexcept -> AnalyzerProjectSource* {
    if (const auto found = model.sources.find(document); found != model.sources.end()) {
        return &found->second;
    }
    return nullptr;
}

auto Analyzer::project(std::string_view document) const noexcept
    -> std::vector<WorkspaceProjectModule> {
    auto result = std::vector<WorkspaceProjectModule>();
    if (const auto* target = target_for(document)) {
        for (const auto& uri : target->documents) {
            const auto found = modules.find(uri);
            if (found != modules.end()) {
                result.push_back(found->second);
            }
        }
        return result;
    }
    for (const auto& [key, entry] : modules) {
        const auto source = model.sources.find(key);
        if ((source != model.sources.end() && source->second.standard)
            || (documents.contains(key) && source == model.sources.end())) {
            result.push_back(entry);
        }
    }
    return result;
}

auto Analyzer::synchronize() noexcept -> void {
    for (auto it = modules.begin(); it != modules.end();) {
        if (!model.sources.contains(it->first) && !documents.contains(it->first)) {
            host.remove(it->first);
            it = modules.erase(it);
        } else {
            ++it;
        }
    }
    for (const auto& [uri, source] : model.sources) {
        publish_source(uri, {});
    }
    for (const auto& [uri, state] : documents.entries()) {
        if (model.sources.contains(uri)) {
            continue;
        }
        publish_source(uri, {});
    }
}

auto Analyzer::publish_source(std::string_view document, std::string_view context) noexcept
    -> void {
    const auto key = std::string(document);
    const auto* disk = disk_source(document);
    const auto state = documents.entries().find(document);
    if (!disk && state == documents.entries().end()) {
        host.remove(document);
        modules.erase(key);
        return;
    }
    const auto* buffer = documents.view(document, context);
    const auto text = buffer ? std::string_view(buffer->text)
        : disk               ? std::string_view(disk->text)
                             : std::string_view();
    if (!host.update(key, ++source_version, std::string(text))) {
        std::terminate();
    }
    modules.insert_or_assign(
        key,
        WorkspaceProjectModule {
            .document = key,
            .module_path = disk ? disk->module_path : state->second.module_path
        }
    );
}

auto Analyzer::prepare_context(std::string_view context) noexcept -> void {
    auto changed = false;
    for (const auto& [document, state] : documents.entries()) {
        if (documents.conflicted(document)) {
            publish_source(document, context);
            changed = true;
        }
    }
    if (changed && !context.empty()) {
        for (auto& target : model.targets) {
            static_cast<void>(resolve_target(target));
        }
    }
}

auto Analyzer::symbol_documents() const noexcept -> std::set<std::string, std::less<>> {
    auto result = std::set<std::string, std::less<>>();
    for (const auto& target : model.targets) {
        if (!preferred_target.empty() && target.name != preferred_target) {
            continue;
        }
        for (const auto& uri : target.documents) {
            const auto source = model.sources.find(uri);
            if (source != model.sources.end() && !source->second.standard) {
                result.insert(uri);
            }
        }
    }
    for (const auto& [uri, state] : documents.entries()) {
        if (!model.sources.contains(uri)) {
            result.insert(uri);
        }
    }
    return result;
}

auto Analyzer::watchers() const noexcept -> std::vector<AnalyzerWatch> {
    auto directories = std::set<std::string>();
    auto scripts = std::set<std::string>();
    const auto directory = [&](const std::filesystem::path& value) noexcept {
        auto error = std::error_code();
        const auto physical = std::filesystem::weakly_canonical(value, error);
        if (!error) {
            directories.insert(path_to_generic_utf8(physical));
        }
    };
    for (const auto& root : workspace_roots) {
        if (const auto path = analyzer_file_path(root)) {
            directory(path_from_utf8(*path));
            auto ancestor = path_from_utf8(*path).lexically_normal();
            for (;;) {
                scripts.insert(path_to_generic_utf8(ancestor / "xmake.lua"));
                scripts.insert(path_to_generic_utf8(ancestor / "build.cv"));
                const auto parent = ancestor.parent_path();
                if (parent.empty() || parent == ancestor) {
                    break;
                }
                ancestor = parent;
            }
        }
    }
    for (const auto& model : project_models) {
        const auto root = model.root;
        for (const auto& value : model.roots) {
            directory(path_from_utf8(value));
        }
        for (const auto& value : model.scripts) {
            auto error = std::error_code();
            const auto physical = std::filesystem::weakly_canonical(
                path_from_utf8(root) / path_from_utf8(value),
                error
            );
            if (!error) {
                scripts.insert(path_to_generic_utf8(physical));
            }
        }
    }
    for (const auto& [uri, source] : model.sources) {
        directory(path_from_utf8(source.path).parent_path());
    }
    auto result = std::vector<AnalyzerWatch>();
    const auto covered = [&](std::string_view value) noexcept {
        return std::ranges::any_of(directories, [&](const auto& parent) noexcept {
            return value != parent && value.starts_with(parent + "/");
        });
    };
    for (const auto& root : directories) {
        if (!covered(root)) {
            result.push_back({.base_uri = analyzer_file_uri(root), .pattern = "**/*.{cv,lua}"});
        }
    }
    for (const auto& script : scripts) {
        // Configuration watches must survive changes to the source directories
        // they define, including deletion and restoration of the build script.
        const auto path = path_from_utf8(script);
        result.push_back(
            {.base_uri = analyzer_file_uri(path_to_generic_utf8(path.parent_path())),
             .pattern = path_to_generic_utf8(path.filename())}
        );
    }
    return result;
}

auto Analyzer::resolve_target(AnalyzerProjectTarget& target) noexcept
    -> std::vector<WorkspaceProjectModule> {
    auto available = std::map<CanonicalModulePath, std::vector<std::string>>();
    auto roots = target.roots;
    for (const auto& uri : target.documents) {
        if (const auto found = modules.find(uri); found != modules.end()) {
            available[found->second.module_path].push_back(uri);
            if (documents.contains(uri)) {
                roots.push_back(found->second.module_path);
            }
        }
    }
    const auto load =
        [&](const CanonicalModulePath& path) noexcept -> std::shared_ptr<const ParsedDocument> {
        auto found = available.find(path);
        if (found == available.end()) {
            const auto input = locate_project_source(path, target.project_root);
            if (!input) {
                return {};
            }
            const auto uri = analyzer_file_uri(input->path);
            if (!modules.contains(uri)) {
                const auto text = read_analyzer_project_source(input->path);
                if (!text) {
                    return {};
                }
                model.sources.insert_or_assign(
                    uri,
                    AnalyzerProjectSource {
                        .path = input->path,
                        .module_path = path,
                        .text = *text,
                        .standard = false
                    }
                );
                publish_source(uri, {});
            }
            target.documents.push_back(uri);
            found = available.emplace(path, std::vector<std::string> {uri}).first;
        }
        const auto parsed = host.snapshot().syntax(found->second.front());
        return parsed ? parsed->result : nullptr;
    };
    auto result = std::vector<WorkspaceProjectModule>();
    for (const auto& path : source_dependency_closure(roots, load)) {
        if (const auto found = available.find(path); found != available.end()) {
            for (const auto& uri : found->second) {
                result.push_back(modules.at(uri));
            }
        }
    }
    return result;
}

auto Analyzer::diagnostics() noexcept -> AnalyzerWorkspace {
    prepare_context({});
    auto resolved = std::map<const AnalyzerProjectTarget*, std::vector<WorkspaceProjectModule>>();
    for (auto& target : model.targets) {
        resolved.emplace(&target, resolve_target(target));
    }
    auto selections = std::vector<std::vector<WorkspaceProjectModule>>();
    auto selected = std::set<const AnalyzerProjectTarget*>();
    for (const auto& target : model.targets) {
        if ((!preferred_target.empty() && target.name == preferred_target)
            || (preferred_target.empty() && target.default_target)) {
            selected.insert(&target);
        }
    }
    auto loose = false;
    for (const auto& [uri, state] : documents.entries()) {
        if (const auto* target = target_for(uri)) {
            selected.insert(target);
        } else {
            loose = true;
        }
    }
    for (const auto* target : selected) {
        selections.push_back(std::move(resolved.at(target)));
    }
    if (loose) {
        selections.push_back(project());
    }
    auto reply = AnalyzerWorkspace {
        .documents = {},
        .diagnostic_texts = {},
        .diagnostics = {},
        .warnings = {},
        .watchers = {}
    };
    auto seen_diagnostics = std::set<std::tuple<
        std::string,
        std::int64_t,
        std::uint32_t,
        std::uint32_t,
        std::string,
        std::string,
        DiagnosticSeverity>>();
    auto seen_documents = std::set<std::string>();
    const auto client_uris = documents.client_uris({});
    for (const auto& selection : selections) {
        const auto conflict = std::ranges::any_of(selection, [&](const auto& input) noexcept {
            return documents.conflicted(input.document);
        });
        const auto result = conflict ? WorkspaceSemanticQuery {.result = {}, .documents = {}}
                                     : host.snapshot().semantic(selection);
        for (const auto& input : selection) {
            const auto state = documents.entries().find(input.document);
            const auto disk = model.sources.find(input.document);
            const auto open = state != documents.entries().end();
            if ((!open && disk != model.sources.end() && disk->second.standard)
                || !seen_documents.insert(input.document).second) {
                continue;
            }
            if (open) {
                for (const auto& [uri, buffer] : state->second.buffers) {
                    reply.documents.push_back(
                        {.uri = uri, .text = buffer.text, .version = buffer.version, .open = true}
                    );
                    if (documents.conflicted(input.document)) {
                        reply.diagnostics.push_back(
                            {.uri = uri,
                             .version = buffer.version,
                             .range = {.start = 0, .end = 0},
                             .message =
                                 "Open aliases of this file have different contents. Close or reconcile the aliases to restore cross-file analysis.",
                             .code = "CV-DOCUMENT-CONFLICT",
                             .severity = DiagnosticSeverity::Error,
                             .related = {},
                             .notes = {}}
                        );
                    }
                }
            } else {
                reply.documents.push_back(
                    {.uri = input.document, .text = disk->second.text, .version = 0, .open = false}
                );
            }
        }
        if (!result.result) {
            continue;
        }
        for (const auto& diagnostic : result.result->diagnostics()) {
            if (!diagnostic.attachment.primary) {
                continue;
            }
            const auto location = diagnostic.attachment.primary->span;
            const auto source = result.result->sources().try_view(location.source_id);
            if (!source || !seen_documents.contains(std::string(source->origin))) {
                continue;
            }
            auto item = AnalyzerDiagnostic {
                .uri = std::string(source->origin),
                .version = 0,
                .range = range(location.span),
                .message = diagnostic.finding.message,
                .code = std::string(diagnostic_code_info(diagnostic.finding.code).name),
                .severity = diagnostic.finding.severity,
                .related = {},
                .notes = {}
            };
            if (!diagnostic.attachment.primary->message.empty()
                && diagnostic.attachment.primary->message != item.message) {
                item.notes.push_back(diagnostic.attachment.primary->message);
            }
            const auto related = [&](SourceSpan span, std::string_view message) noexcept {
                if (const auto source = result.result->sources().try_view(span.source_id)) {
                    const auto origin = source->origin;
                    auto uri = std::string(origin);
                    if (const auto found = client_uris.find(uri); found != client_uris.end()) {
                        uri = found->second;
                    }
                    if (!seen_documents.contains(uri) && !reply.diagnostic_texts.contains(uri)) {
                        reply.diagnostic_texts.emplace(uri, source->text);
                    }
                    item.related.push_back(
                        {.uri = std::move(uri),
                         .range = range(span.span),
                         .message = std::string(message)}
                    );
                }
            };
            for (const auto& label : diagnostic.attachment.related) {
                related(label.span, label.message);
            }
            for (const auto& note : diagnostic.attachment.notes) {
                if (note.span) {
                    related(*note.span, note.message);
                } else {
                    item.notes.push_back(note.message);
                }
            }
            for (const auto& help : diagnostic.attachment.helps) {
                item.notes.push_back(help);
            }
            if (seen_diagnostics
                    .emplace(
                        item.uri,
                        item.version,
                        item.range.start,
                        item.range.end,
                        item.message,
                        item.code,
                        item.severity
                    )
                    .second) {
                if (const auto state = documents.entries().find(item.uri);
                    state != documents.entries().end()) {
                    for (const auto& [uri, buffer] : state->second.buffers) {
                        auto projected = item;
                        projected.uri = uri;
                        projected.version = buffer.version;
                        reply.diagnostics.push_back(std::move(projected));
                    }
                } else {
                    reply.diagnostics.push_back(std::move(item));
                }
            }
        }
    }
    auto warnings = model.warnings;
    for (const auto& project : project_models) {
        if (!project.available) {
            warnings.push_back(
                "Could not query Carven project: " + project.directory
                + ". Check build.cv / carven query, or the configured Xmake command."
            );
        }
    }
    if (!preferred_target.empty()
        && std::ranges::none_of(model.targets, [&](const auto& target) noexcept {
               return target.name == preferred_target;
           })) {
        warnings.push_back("Carven target not found: " + preferred_target);
    }
    reply.warnings = std::move(warnings);
    return reply;
}

auto Analyzer::execute(const AnalyzerRequest& request) noexcept -> AnalyzerResult {
    const auto method = request.method;
    if (method == AnalyzerMethod::Hello) {
        return AnalyzerHello {.version = 9};
    }
    if (method == AnalyzerMethod::Shutdown) {
        return std::monostate {};
    }
    if (method == AnalyzerMethod::Discover) {
        return AnalyzerProjects {.projects = discover_analyzer_projects(request.roots)};
    }
    if (method == AnalyzerMethod::Workspace) {
        workspace_roots = request.roots;
        if (request.projects) {
            auto next = *request.projects;
            for (auto& project : next) {
                if (project.available) {
                    continue;
                }
                // Failed reloads invalidate semantics, not the dependencies
                // needed to observe a later repair. Never restore old targets.
                const auto previous = std::ranges::find(
                    project_models,
                    project.directory,
                    &AnalyzerProjectInput::directory
                );
                if (previous != project_models.end()) {
                    project.root = previous->root;
                    project.roots = previous->roots;
                    project.scripts = previous->scripts;
                }
            }
            project_models = std::move(next);
        }
        preferred_target = request.target;
        model = load_analyzer_project(project_models);
        synchronize();
        ++revision;
        auto reply = diagnostics();
        reply.watchers = watchers();
        return reply;
    }
    const auto uri = documents.resolve(request.uri);
    if (uri.empty() && method != AnalyzerMethod::WorkspaceSymbols) {
        return error("invalid document");
    }
    if (method == AnalyzerMethod::Update) {
        const auto* disk = disk_source(uri);
        const auto path = disk ? disk->module_path : documents.module_path(uri, workspace_roots);
        const auto changed = request.open
            ? documents.open(request.uri, uri, path, request.version, request.text)
            : documents.change(request.uri, request.version, request.text);
        if (!changed) {
            return diagnostics();
        }
        publish_source(uri, {});
        ++revision;
        return diagnostics();
    }
    if (method == AnalyzerMethod::Close) {
        const auto closed = documents.close(request.uri);
        if (!closed) {
            return diagnostics();
        }
        if (!documents.contains(*closed)) {
            if (auto* disk = disk_source(*closed)) {
                if (const auto text = read_analyzer_project_source(disk->path)) {
                    disk->text = *text;
                } else {
                    model.sources.erase(*closed);
                }
            }
        }
        publish_source(*closed, {});
        ++revision;
        return diagnostics();
    }
    if (request.revision != revision) {
        return error("invalid query revision");
    }
    const auto context = request.context.empty() ? request.uri : request.context;
    prepare_context(context);
    const auto snapshot = host.snapshot();
    if (method == AnalyzerMethod::FoldingRanges) {
        auto reply = AnalyzerTextItems<WorkspaceFold> {.text = {}, .items = {}};
        if (const auto query = snapshot.folding_ranges(uri)) {
            reply.text = query->document.result->source().text;
            reply.items = *query->result;
        }
        return reply;
    }
    const auto selection =
        project(request.context.empty() ? uri : documents.resolve(request.context));
    const auto syntax_only =
        method == AnalyzerMethod::Formatting || method == AnalyzerMethod::DocumentSymbols;
    const auto changing = method == AnalyzerMethod::Rename
        || method == AnalyzerMethod::PrepareRename
        || method == AnalyzerMethod::CodeActions;
    const auto symbol_conflict = method == AnalyzerMethod::WorkspaceSymbols
        && std::ranges::any_of(symbol_documents(), [&](const auto& document) noexcept {
                                     return documents.conflicted(document);
                                 });
    if (symbol_conflict
        || (!syntax_only && std::ranges::any_of(selection, [&](const auto& input) noexcept {
               return documents.conflicted(input.document)
                   && (changing || documents.view(input.document, context) == nullptr);
           }))) {
        return std::unexpected(AnalyzerFailure(
            AnalyzerQueryFailure {
                .code = AnalyzerQueryError::DocumentConflict,
                .message =
                    "Open aliases have different contents; reconcile or close the aliases before querying dependent files or applying cross-file edits"
            }
        ));
    }
    if (method == AnalyzerMethod::PrepareCallHierarchy
        || method == AnalyzerMethod::IncomingCalls
        || method == AnalyzerMethod::OutgoingCalls) {
        auto reply = AnalyzerCalls {.callable = std::nullopt, .calls = {}, .documents = {}};
        auto query = method == AnalyzerMethod::PrepareCallHierarchy
            ? snapshot.prepare_call_hierarchy(selection, uri, request.offset)
            : method == AnalyzerMethod::IncomingCalls
            ? snapshot.incoming_calls(selection, uri, request.offset)
            : snapshot.outgoing_calls(selection, uri, request.offset);
        auto seen = std::set<std::string>();
        const auto locate = [&](WorkspaceCallable& callable) noexcept {
            const auto& document = callable.location.document;
            const auto source = query.analysis.result->source(document);
            if (seen.insert(document).second && source) {
                reply.documents.push_back(
                    {.uri = document,
                     .text = std::string(source->text),
                     .version = 0,
                     .open = false}
                );
            }
        };
        if (query.item) {
            locate(*query.item);
        }
        for (auto& call : query.calls) {
            locate(call.callable);
        }
        reply.callable = std::move(query.item);
        reply.calls = std::move(query.calls);
        return reply;
    }
    if (method == AnalyzerMethod::DocumentHighlights) {
        auto reply = AnalyzerTextItems<AnalyzerRange> {.text = {}, .items = {}};
        const auto query = snapshot.document_highlights(selection, uri, request.offset);
        if (query.analysis.result) {
            if (const auto source = query.analysis.result->source(uri)) {
                reply.text = source->text;
            }
        }
        for (const auto span : query.result) {
            reply.items.push_back(range(span));
        }
        return reply;
    }
    const auto editable = [&](std::string_view document) noexcept {
        const auto disk = model.sources.find(std::string(document));
        return disk == model.sources.end() || !disk->second.standard;
    };
    if (method == AnalyzerMethod::Formatting) {
        auto reply = AnalyzerTextItems<AnalyzerEdit> {.text = {}, .items = {}};
        const auto syntax = snapshot.syntax(uri);
        if (!syntax || !editable(uri)) {
            return reply;
        }
        reply.text = syntax->result->source().text;
        const auto formatted = snapshot.formatting(uri);
        if (!formatted->result) {
            return std::unexpected(AnalyzerFailure(
                AnalyzerQueryFailure {
                    .code = AnalyzerQueryError::IncompleteSyntax,
                    .message = formatted->result.error().empty()
                        ? "Formatting requires complete syntax"
                        : formatted->result.error().front().finding.message
                }
            ));
        }
        if (const auto& edit = *formatted->result) {
            reply.items.push_back({.range = range(edit->range), .replacement = edit->replacement});
        }
        return reply;
    }
    if (method == AnalyzerMethod::CodeActions
        || method == AnalyzerMethod::SemanticTokens
        || method == AnalyzerMethod::InlayHints) {
        const auto syntax = snapshot.syntax(uri);
        const auto text = syntax ? syntax->result->source().text : std::string_view();
        const auto begin = request.start.value_or(0);
        const auto end = request.end.value_or(static_cast<std::uint32_t>(text.size()));
        if (syntax && (begin > end || end > text.size())) {
            return error("invalid query range");
        }
        if (method == AnalyzerMethod::CodeActions) {
            auto reply = AnalyzerTextItems<AnalyzerAction> {.text = {}, .items = {}};
            if (!syntax || !editable(uri)) {
                return reply;
            }
            reply.text = text;
            const auto query = snapshot.code_actions(
                selection,
                uri,
                Span::from_bounds(begin, end),
                request.quick_fix,
                request.organize_imports
            );
            for (const auto& action : query.result) {
                if (std::ranges::any_of(action.edits, [&](const auto& edit) noexcept {
                        return !editable(edit.document);
                    })) {
                    continue;
                }
                auto item = AnalyzerAction {
                    .title = action.title,
                    .kind = action.kind,
                    .diagnostic = std::nullopt,
                    .documents = {}
                };
                if (action.finding) {
                    item.diagnostic = AnalyzerDiagnostic {
                        .uri = uri,
                        .version = 0,
                        .range = range(action.diagnostic_range),
                        .message = action.finding->message,
                        .code = std::string(diagnostic_code_info(action.finding->code).name),
                        .severity = action.finding->severity,
                        .related = {},
                        .notes = {}
                    };
                }
                auto edits = std::map<std::string, std::vector<AnalyzerEdit>>();
                for (const auto& edit : action.edits) {
                    edits[edit.document].push_back(
                        {.range = range(edit.range), .replacement = edit.replacement}
                    );
                }
                for (auto& [document, changes] : edits) {
                    const auto source = query.analysis.result->source(document);
                    item.documents.push_back(
                        {.uri = document,
                         .text = std::string(source->text),
                         .edits = std::move(changes)}
                    );
                }
                reply.items.push_back(std::move(item));
            }
            return reply;
        }
        if (method == AnalyzerMethod::SemanticTokens) {
            auto reply = AnalyzerTextItems<AnalyzerToken> {.text = std::string(text), .items = {}};
            if (syntax) {
                const auto query =
                    snapshot.decorations(selection, uri, Span::from_bounds(begin, end));
                for (const auto& token : query.result.highlights) {
                    reply.items.push_back(
                        {.range = range(token.range),
                         .kind = static_cast<std::uint32_t>(token.kind),
                         .modifiers = token.modifiers}
                    );
                }
            }
            return reply;
        }
        auto reply = AnalyzerTextItems<AnalyzerHint> {.text = std::string(text), .items = {}};
        if (syntax) {
            const auto query = snapshot.decorations(selection, uri, Span::from_bounds(begin, end));
            for (const auto& hint : query.result.hints) {
                reply.items.push_back(
                    {.position = hint.position, .label = hint.label, .kind = hint.kind}
                );
            }
        }
        return reply;
    }
    if (method == AnalyzerMethod::DocumentSymbols || method == AnalyzerMethod::WorkspaceSymbols) {
        auto reply = AnalyzerSymbols {.symbols = {}, .documents = {}};
        if (method == AnalyzerMethod::DocumentSymbols) {
            const auto query = snapshot.document_symbols(uri);
            if (query && query->result) {
                reply.documents.push_back(
                    {.uri = uri,
                     .text = std::string(query->document.result->source().text),
                     .version = 0,
                     .open = false}
                );
                for (const auto& symbol : *query->result) {
                    reply.symbols.push_back(document_symbol(symbol));
                }
            }
        } else {
            const auto visible = symbol_documents();
            auto seen = std::set<std::string>();
            for (const auto& symbol : *snapshot.workspace_symbols()) {
                if (!visible.contains(symbol.document)
                    || !matches_symbol(symbol.name, request.query)) {
                    continue;
                }
                if (seen.insert(symbol.document).second) {
                    reply.documents.push_back(
                        {.uri = symbol.document,
                         .text =
                             std::string(snapshot.syntax(symbol.document)->result->source().text),
                         .version = 0,
                         .open = false}
                    );
                }
                reply.symbols.push_back(
                    {.name = symbol.name,
                     .kind = symbol.kind,
                     .uri = symbol.document,
                     .range = range(symbol.selection),
                     .selection = range(symbol.selection),
                     .children = {}}
                );
            }
        }
        return reply;
    }
    if (method == AnalyzerMethod::SignatureHelp) {
        const auto query = snapshot.signature_help(selection, uri, request.offset);
        auto reply = std::optional<AnalyzerSignature>();
        if (query.result) {
            reply = AnalyzerSignature {
                .label = query.result->label,
                .parameters = query.result->parameters,
                .active_parameter = query.result->active_parameter
            };
        }
        return reply;
    }
    if (method == AnalyzerMethod::PrepareRename || method == AnalyzerMethod::Rename) {
        auto reply = std::optional<AnalyzerRename>();
        auto query = snapshot.prepare_rename(selection, uri, request.offset);
        if (query.result
            && (!editable(query.result->declaration.document)
                || std::ranges::any_of(
                    query.result->references,
                    [&](const auto& location) noexcept { return !editable(location.document); }
                ))) {
            query.result.reset();
        }
        if (method == AnalyzerMethod::Rename && query.result) {
            auto checked = snapshot.rename(selection, uri, request.offset, request.new_name);
            if (!checked) {
                const auto invalid = checked.error() == WorkspaceRenameError::InvalidName;
                return std::unexpected(AnalyzerFailure(
                    AnalyzerQueryFailure {
                        .code = invalid ? AnalyzerQueryError::InvalidName
                                        : AnalyzerQueryError::BindingChange,
                        .message = invalid
                            ? "New name must be a non-reserved Carven identifier"
                            : "Rename changes an existing binding or cannot preserve its references"
                    }
                ));
            }
            query = std::move(*checked);
        }
        if (query.result) {
            reply = AnalyzerRename {
                .range = range(query.result->location.range),
                .placeholder = query.result->name,
                .documents = {}
            };
            if (method == AnalyzerMethod::PrepareRename) {
                return reply;
            }
            auto edits = std::map<std::string, std::vector<AnalyzerEdit>>();
            for (const auto& location : query.result->references) {
                edits[location.document].push_back(
                    {.range = range(location.range), .replacement = request.new_name}
                );
            }
            for (auto& [document, changes] : edits) {
                const auto source = query.analysis.result->source(document);
                reply->documents.push_back(
                    {.uri = document,
                     .text = std::string(source->text),
                     .edits = std::move(changes)}
                );
            }
        }
        return reply;
    }
    if (method == AnalyzerMethod::Completion) {
        const auto query = snapshot.completion(selection, uri, request.offset);
        auto reply =
            AnalyzerCompletions {.replacement = range(query.replacement), .completions = {}};
        for (const auto& candidate : query.candidates) {
            reply.completions.push_back(
                {.label = candidate.name,
                 .kind = candidate.kind,
                 .detail = candidate.detail,
                 .import_module = {},
                 .import_edit = std::nullopt}
            );
        }
        for (const auto& imported : query.imports) {
            const auto& candidate = imported.candidate;
            reply.completions.push_back(
                {.label = candidate.name,
                 .kind = candidate.kind,
                 .detail = candidate.detail,
                 .import_module = imported.module_path,
                 .import_edit = AnalyzerEdit {
                     .range = range(imported.edit.range),
                     .replacement = imported.edit.replacement
                 }}
            );
        }
        return reply;
    }
    if (method == AnalyzerMethod::Definition || method == AnalyzerMethod::References) {
        auto reply = AnalyzerLocations {.locations = {}, .documents = {}};
        auto seen_documents = std::set<std::string>();
        const auto locate =
            [&](const WorkspaceSemanticAnalysis& analysis,
                const WorkspaceVersionedLocation& value) noexcept -> AnalyzerLocation {
            auto uri = value.document;
            if (seen_documents.insert(uri).second) {
                const auto source = analysis.source(value.document);
                reply.documents.push_back(
                    {.uri = uri,
                     .text = source ? std::string(source->text) : std::string(),
                     .version = 0,
                     .open = false}
                );
            }
            return {.uri = std::move(uri), .range = range(value.range)};
        };
        if (method == AnalyzerMethod::Definition) {
            const auto query = snapshot.definition(selection, uri, request.offset);
            if (query.result) {
                reply.locations.push_back(locate(*query.analysis.result, *query.result));
            }
            return reply;
        }
        if (method == AnalyzerMethod::References) {
            const auto query =
                snapshot.references(selection, uri, request.offset, request.include_declaration);
            if (query.result) {
                for (const auto& location : *query.result) {
                    reply.locations.push_back(locate(*query.analysis.result, location));
                }
            }
            return reply;
        }
    }
    if (method == AnalyzerMethod::Hover) {
        auto reply = std::optional<AnalyzerHover>();
        const auto query = snapshot.hover(selection, uri, request.offset);
        if (query.result && !query.result->description.empty()) {
            reply = AnalyzerHover {
                .text = query.result->description,
                .uri = query.result->location.document,
                .range = range(query.result->location.range),
                .documentation = query.result->documentation
            };
        }
        return reply;
    }
    return error("unknown analyzer method");
}

} // namespace

extern "C++" auto main(int argc, char**) noexcept -> int {
#if defined(_WIN32)
    ::_setmode(::_fileno(stdin), _O_BINARY);
    ::_setmode(::_fileno(stdout), _O_BINARY);
#endif
    if (argc != 1) {
        std::println(std::cerr, "Usage: carven-analyzer");
        return 2;
    }
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    auto analyzer = Analyzer();
    if (const auto failure = analyzer.initialize()) {
        std::println(std::cerr, "carven-analyzer: {}", *failure);
        return 2;
    }
    while (true) {
        auto header = std::array<unsigned char, 4>();
        std::cin.read(reinterpret_cast<char*>(header.data()), 4);
        if (std::cin.gcount() == 0 && std::cin.eof()) {
            return 0;
        }
        if (std::cin.gcount() != 4) {
            return 2;
        }
        auto size = std::uint32_t(0);
        for (const auto byte : header) {
            size = (size << 8) | byte;
        }
        if (size == 0 || size > message_limit) {
            return 2;
        }
        auto bytes = std::vector<std::uint8_t>(size);
        if (!std::cin.read(reinterpret_cast<char*>(bytes.data()), size)) {
            return 2;
        }
        const auto request = decode_analyzer_request(bytes);
        if (!request) {
            return 2;
        }
        auto result = analyzer.execute(*request);
        const auto reply =
            AnalyzerReply {.revision = analyzer.current_revision(), .result = std::move(result)};
        const auto output = encode_analyzer_reply(reply, analyzer.client_document_uris(*request));
        if (output.empty() || output.size() > message_limit) {
            return 2;
        }
        for (auto index = 0uz; index < 4; ++index) {
            header[index] = static_cast<unsigned char>(output.size() >> ((3 - index) * 8));
        }
        std::cout.write(reinterpret_cast<const char*>(header.data()), 4);
        std::cout.write(
            reinterpret_cast<const char*>(output.data()),
            static_cast<std::streamsize>(output.size())
        );
        std::cout.flush();
        if (!std::cout) {
            return 2;
        }
        if (request->method == AnalyzerMethod::Shutdown) {
            return 0;
        }
    }
}
