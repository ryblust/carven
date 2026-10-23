module carven:workspace.analysis.impl;

import :diagnostics.diagnostic;
import :frontend.document;
import :frontend.lex.token;
import :frontend.program.dependencies;
import :semantic.analysis.source.imports;
import :source.document;
import :source.identifier;
import :source.standard;
import :source.text;
import :support.timing;
import :workspace.analysis;
import :workspace.decorations;
import :workspace.folding;
import :workspace.semantic;
import :workspace.symbols;
import std;

namespace {

class DocumentQueries final {
public:
    explicit DocumentQueries(std::shared_ptr<const SourceDocument> source) noexcept;
    auto source() const noexcept -> SourceView;
    auto source_owner() const noexcept -> std::shared_ptr<const SourceDocument>;
    auto syntax(TimingOutput timings = {}) noexcept -> std::shared_ptr<const ParsedDocument>;
    auto symbols() noexcept -> std::shared_ptr<const WorkspaceDocumentSymbolList>;
    auto folds() noexcept -> std::shared_ptr<const WorkspaceFoldList>;

private:
    std::shared_ptr<const SourceDocument> input;
    std::shared_ptr<const ParsedDocument> cached_syntax;
    std::shared_ptr<const WorkspaceDocumentSymbolList> cached_symbols;
    std::shared_ptr<const WorkspaceFoldList> cached_folds;
};

struct DocumentEntry final {
    std::int64_t version;
    std::shared_ptr<DocumentQueries> queries;
};

using DocumentMap = std::map<std::string, DocumentEntry, std::less<>>;

class CachedAnalysis final {
public:
    explicit CachedAnalysis(std::vector<WorkspaceSemanticInput> inputs) noexcept;
    auto matches_documents(const DocumentMap& documents) const noexcept -> bool;
    auto matches_modules(std::span<const WorkspaceProjectModule> modules) const noexcept -> bool;
    auto result(TimingOutput timings) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
    auto source_result() noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
    auto has_complete_source_analysis() const noexcept -> bool;
    auto recover_within(
        std::span<const WorkspaceProjectModule> modules,
        std::string_view document
    ) noexcept -> bool;

private:
    std::vector<WorkspaceSemanticInput> inputs;
    std::shared_ptr<const WorkspaceSemanticAnalysis> cached_result;
    std::shared_ptr<const WorkspaceSemanticAnalysis> cached_sources;
};

struct CachedCompletion final {
    std::vector<WorkspaceSemanticInput> inputs;
    WorkspaceDocumentLocation cursor;
    std::shared_ptr<const WorkspaceSemanticAnalysis> result;
};

struct CompletionCache final {
    std::vector<CachedCompletion> entries;
};

} // namespace

class WorkspaceQueries final {
    struct ResolvedSelection final {
        std::shared_ptr<CachedAnalysis> node;
        std::vector<WorkspaceDocumentVersion> versions;
    };

public:
    WorkspaceQueries(
        DocumentMap inputs,
        std::vector<std::shared_ptr<CachedAnalysis>> cached_selections,
        std::size_t semantic_cache_capacity,
        std::shared_ptr<CompletionCache> completions = {}
    ) noexcept;
    auto document(std::string_view key) const noexcept -> std::optional<DocumentEntry>;
    auto documents() const noexcept -> DocumentMap;
    auto symbols() noexcept -> std::shared_ptr<const WorkspaceSymbolList>;
    auto resolve(
        std::span<const WorkspaceProjectModule> project,
        TimingOutput timings = {}
    ) noexcept -> ResolvedSelection;
    auto selections() const noexcept -> std::vector<std::shared_ptr<CachedAnalysis>>;
    auto capacity() const noexcept -> std::size_t;
    auto completion_cache() const noexcept -> std::shared_ptr<CompletionCache>;
    auto complete(
        std::vector<WorkspaceSemanticInput> inputs,
        WorkspaceDocumentLocation cursor
    ) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;
    auto navigation(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document
    ) noexcept -> std::optional<ResolvedSelection>;
    auto navigation_modules(
        std::span<const WorkspaceProjectModule> project,
        std::string_view document
    ) noexcept -> std::optional<std::vector<WorkspaceProjectModule>>;

private:
    DocumentMap inputs;
    std::shared_ptr<const WorkspaceSymbolList> cached_symbols;
    std::vector<std::shared_ptr<CachedAnalysis>> cached_selections;
    std::size_t semantic_cache_capacity;
    std::shared_ptr<CompletionCache> completions;
};

DocumentQueries::DocumentQueries(std::shared_ptr<const SourceDocument> source) noexcept
    : input(std::move(source)) {}

auto DocumentQueries::source() const noexcept -> SourceView {
    return input->source();
}

auto DocumentQueries::source_owner() const noexcept -> std::shared_ptr<const SourceDocument> {
    return input;
}

auto DocumentQueries::syntax(TimingOutput timings) noexcept
    -> std::shared_ptr<const ParsedDocument> {
    if (!cached_syntax) {
        cached_syntax = parse_document(input, timings);
    }
    return cached_syntax;
}

auto DocumentQueries::symbols() noexcept -> std::shared_ptr<const WorkspaceDocumentSymbolList> {
    if (!cached_symbols) {
        const auto parsed = syntax();
        if (parsed->recovered_syntax()) {
            cached_symbols = std::make_shared<const WorkspaceDocumentSymbolList>(
                collect_workspace_document_symbols(*parsed)
            );
        }
    }
    return cached_symbols;
}

auto DocumentQueries::folds() noexcept -> std::shared_ptr<const WorkspaceFoldList> {
    if (!cached_folds) {
        cached_folds =
            std::make_shared<const WorkspaceFoldList>(collect_workspace_folds(*syntax()));
    }
    return cached_folds;
}

WorkspaceQueries::WorkspaceQueries(
    DocumentMap inputs,
    std::vector<std::shared_ptr<CachedAnalysis>> cached_selections,
    std::size_t semantic_cache_capacity,
    std::shared_ptr<CompletionCache> completions
) noexcept
    : inputs(std::move(inputs)),
      cached_selections(std::move(cached_selections)),
      semantic_cache_capacity(semantic_cache_capacity),
      completions(completions ? std::move(completions) : std::make_shared<CompletionCache>()) {
    std::erase_if(this->cached_selections, [&](const auto& selection) noexcept {
        return !selection->matches_documents(this->inputs);
    });
}

auto WorkspaceQueries::document(std::string_view key) const noexcept
    -> std::optional<DocumentEntry> {
    const auto found = inputs.find(key);
    if (found == inputs.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto WorkspaceQueries::documents() const noexcept -> DocumentMap {
    return inputs;
}

auto WorkspaceQueries::symbols() noexcept -> std::shared_ptr<const WorkspaceSymbolList> {
    if (!cached_symbols) {
        auto result = WorkspaceSymbolList();
        for (const auto& [key, entry] : inputs) {
            const auto symbols = entry.queries->symbols();
            if (!symbols) {
                continue;
            }
            for (const auto& symbol : *symbols) {
                result.push_back(
                    WorkspaceSymbol {
                        .document = key,
                        .name = symbol.name,
                        .kind = symbol.kind,
                        .range = symbol.range,
                        .selection = symbol.selection,
                    }
                );
            }
        }
        cached_symbols = std::make_shared<const WorkspaceSymbolList>(std::move(result));
    }
    return cached_symbols;
}

CachedAnalysis::CachedAnalysis(std::vector<WorkspaceSemanticInput> inputs) noexcept
    : inputs(std::move(inputs)) {}

auto CachedAnalysis::matches_documents(const DocumentMap& documents) const noexcept -> bool {
    return std::ranges::all_of(inputs, [&](const WorkspaceSemanticInput& input) noexcept {
        const auto found = documents.find(input.project_module.document);
        const auto source =
            found == documents.end() ? nullptr : found->second.queries->source_owner();
        return input.source == source;
    });
}

auto CachedAnalysis::matches_modules(std::span<const WorkspaceProjectModule> modules) const noexcept
    -> bool {
    return std::ranges::equal(
        inputs,
        modules,
        [](const WorkspaceSemanticInput& input,
           const WorkspaceProjectModule& project_module) static noexcept {
            return input.project_module == project_module;
        }
    );
}

auto CachedAnalysis::result(TimingOutput timings) noexcept
    -> std::shared_ptr<const WorkspaceSemanticAnalysis> {
    if (!cached_result) {
        cached_result = analyze_workspace_project(inputs, false, timings);
    }
    return cached_result;
}

auto CachedAnalysis::has_complete_source_analysis() const noexcept -> bool {
    return cached_result && cached_result->source_construction_complete();
}

auto CachedAnalysis::recover_within(
    std::span<const WorkspaceProjectModule> modules,
    std::string_view document
) noexcept -> bool {
    const auto within = [&](const WorkspaceSemanticAnalysis& result) noexcept {
        for (const auto& diagnostic : result.diagnostics()) {
            if (diagnostic.finding.severity != DiagnosticSeverity::Error) {
                continue;
            }
            if (!diagnostic.attachment.primary) {
                return false;
            }
            const auto source =
                result.sources().try_view(diagnostic.attachment.primary->span.source_id);
            if (!source || std::ranges::none_of(modules, [&](const auto& project_module) noexcept {
                    return project_module.document == source->origin;
                })) {
                return false;
            }
        }
        return true;
    };
    if (!cached_result || !within(*cached_result)) {
        return false;
    }
    const auto recovered = source_result();
    // Recovery can reveal further errors. Keep the local fallback if those come
    // from another module, even when earlier steps retained declaration facts.
    return within(*recovered) && !recovered->occurrences(document).empty();
}

auto CachedAnalysis::source_result() noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis> {
    auto strict = result({});
    if (strict->source_construction_complete()) {
        return strict;
    }
    if (!cached_sources) {
        cached_sources = analyze_workspace_source_queries(inputs, {}, strict);
    }
    return cached_sources;
}

auto WorkspaceQueries::resolve(
    std::span<const WorkspaceProjectModule> project,
    TimingOutput timings
) noexcept -> ResolvedSelection {
    auto modules = std::vector<WorkspaceProjectModule>(project.begin(), project.end());
    std::ranges::sort(
        modules,
        [](const WorkspaceProjectModule& left,
           const WorkspaceProjectModule& right) static noexcept {
            return std::pair(left.module_path.value(), std::string_view(left.document))
                < std::pair(right.module_path.value(), std::string_view(right.document));
        }
    );
    const auto found = std::ranges::find_if(cached_selections, [&](const auto& selection) noexcept {
        return selection->matches_modules(modules);
    });
    auto node = std::shared_ptr<CachedAnalysis>();
    if (found != cached_selections.end()) {
        node = *found;
        cached_selections.erase(found);
    } else {
        auto resolved = std::vector<WorkspaceSemanticInput>();
        for (const auto& project_module : modules) {
            const auto entry = document(project_module.document);
            resolved.push_back({
                .project_module = project_module,
                .source = entry ? entry->queries->source_owner() : nullptr,
                .parsed = entry ? entry->queries->syntax(timings) : nullptr,
                .excluded = {},
            });
        }
        node = std::make_shared<CachedAnalysis>(std::move(resolved));
        if (semantic_cache_capacity != 0uz && cached_selections.size() == semantic_cache_capacity) {
            cached_selections.erase(cached_selections.begin());
        }
    }
    if (semantic_cache_capacity != 0uz) {
        cached_selections.push_back(node);
    }
    auto versions = std::map<std::string_view, std::int64_t>();
    for (const auto& project_module : modules) {
        if (const auto entry = document(project_module.document)) {
            versions.emplace(project_module.document, entry->version);
        }
    }
    auto document_versions = std::vector<WorkspaceDocumentVersion>();
    for (const auto& [document, version] : versions) {
        document_versions.push_back({.document = std::string(document), .version = version});
    }
    return {.node = std::move(node), .versions = std::move(document_versions)};
}

auto WorkspaceQueries::capacity() const noexcept -> std::size_t {
    return semantic_cache_capacity;
}

auto WorkspaceQueries::completion_cache() const noexcept -> std::shared_ptr<CompletionCache> {
    return completions;
}

auto WorkspaceQueries::complete(
    std::vector<WorkspaceSemanticInput> inputs,
    WorkspaceDocumentLocation cursor
) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis> {
    const auto matches = [&](const CachedCompletion& cached) {
        return cached.cursor.document == cursor.document
            && cached.cursor.range == cursor.range
            && std::ranges::equal(cached.inputs, inputs, [](const auto& left, const auto& right) {
                   return left.project_module == right.project_module
                       && (left.source == right.source
                           || (left.source
                               && right.source
                               && left.source->source().text == right.source->source().text));
               });
    };
    if (const auto found = std::ranges::find_if(completions->entries, matches);
        found != completions->entries.end()) {
        auto hit = std::move(*found);
        completions->entries.erase(found);
        const auto result = hit.result;
        completions->entries.push_back(std::move(hit));
        return result;
    }
    const auto result = analyze_workspace_source_queries(inputs, cursor);
    if (semantic_cache_capacity != 0) {
        if (completions->entries.size() == semantic_cache_capacity) {
            completions->entries.erase(completions->entries.begin());
        }
        completions->entries.push_back({std::move(inputs), std::move(cursor), result});
    }
    return result;
}

auto WorkspaceQueries::selections() const noexcept -> std::vector<std::shared_ptr<CachedAnalysis>> {
    return cached_selections;
}

auto WorkspaceQueries::navigation_modules(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document
) noexcept -> std::optional<std::vector<WorkspaceProjectModule>> {
    auto by_path = std::map<std::string_view, const WorkspaceProjectModule*, std::less<>>();
    auto documents = std::set<std::string_view>();
    const auto* root = static_cast<const WorkspaceProjectModule*>(nullptr);
    auto valid_project = true;
    for (const auto& project_module : project) {
        if (!by_path.emplace(project_module.module_path.value(), &project_module).second
            || !documents.insert(project_module.document).second) {
            valid_project = false;
        }
        if (project_module.document == document) {
            root = &project_module;
        }
    }
    if (root == nullptr) {
        return std::nullopt;
    }
    if (!valid_project) {
        return std::vector<WorkspaceProjectModule>(project.begin(), project.end());
    }
    const auto load =
        [&](const CanonicalModulePath& path) noexcept -> std::shared_ptr<const ParsedDocument> {
        const auto found = by_path.find(path.value());
        if (found == by_path.end()) {
            return nullptr;
        }
        const auto entry = this->document(found->second->document);
        return entry ? entry->queries->syntax() : nullptr;
    };
    const auto selected = source_dependency_closure(std::span(&root->module_path, 1uz), load);
    auto modules = std::vector<WorkspaceProjectModule>();
    for (const auto& path : selected) {
        if (const auto found = by_path.find(path.value()); found != by_path.end()) {
            modules.push_back(*found->second);
        }
    }
    return modules;
}

auto WorkspaceQueries::navigation(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document
) noexcept -> std::optional<ResolvedSelection> {
    // Reuse this exact target's complete facts, including validation failures.
    // A partial target can share recovery when its errors belong to the import
    // closure; unrelated failures must still allow an independent local query.
    auto target = std::vector<WorkspaceProjectModule>(project.begin(), project.end());
    std::ranges::sort(target, [](const auto& left, const auto& right) static noexcept {
        return std::pair(left.module_path.value(), std::string_view(left.document))
            < std::pair(right.module_path.value(), std::string_view(right.document));
    });
    if (std::ranges::none_of(target, [&](const auto& project_module) noexcept {
            return project_module.document == document;
        })) {
        return std::nullopt;
    }
    const auto cached =
        std::ranges::find_if(cached_selections, [&](const auto& candidate) noexcept {
            return candidate->matches_modules(target);
        });
    if (cached != cached_selections.end() && (*cached)->has_complete_source_analysis()) {
        return resolve(target);
    }
    const auto modules = navigation_modules(project, document);
    if (!modules) {
        return std::nullopt;
    }
    if (cached != cached_selections.end() && (*cached)->recover_within(*modules, document)) {
        return resolve(target);
    }
    return resolve(*modules);
}

WorkspaceAnalysisSnapshot::WorkspaceAnalysisSnapshot(
    std::shared_ptr<WorkspaceQueries> queries
) noexcept
    : queries(std::move(queries)) {}

auto WorkspaceAnalysisSnapshot::syntax(std::string_view document) const noexcept
    -> std::optional<WorkspaceSyntaxQuery> {
    const auto entry = queries->document(document);
    if (!entry) {
        return std::nullopt;
    }
    return WorkspaceSyntaxQuery {.version = entry->version, .result = entry->queries->syntax()};
}

auto WorkspaceAnalysisSnapshot::document_symbols(std::string_view document) const noexcept
    -> std::optional<WorkspaceSymbolsQuery> {
    const auto entry = queries->document(document);
    if (!entry) {
        return std::nullopt;
    }
    return WorkspaceSymbolsQuery {
        .document =
            WorkspaceSyntaxQuery {.version = entry->version, .result = entry->queries->syntax()},
        .result = entry->queries->symbols(),
    };
}

auto WorkspaceAnalysisSnapshot::folding_ranges(std::string_view document) const noexcept
    -> std::optional<WorkspaceFoldingQuery> {
    const auto entry = queries->document(document);
    if (!entry) {
        return std::nullopt;
    }
    return WorkspaceFoldingQuery {
        .document = {.version = entry->version, .result = entry->queries->syntax()},
        .result = entry->queries->folds()
    };
}

auto WorkspaceAnalysisSnapshot::document_highlights(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceHighlightsQuery {
    auto resolved = queries->navigation(project, document);
    if (!resolved) {
        return {.analysis = {.result = nullptr, .documents = {}}, .result = {}};
    }
    auto analysis = WorkspaceSemanticQuery {
        .result = resolved->node->source_result(),
        .documents = std::move(resolved->versions)
    };
    auto references = analysis.result->alias_references(document, offset);
    if (!references) {
        references = analysis.result->references(document, offset);
    }
    auto result = std::vector<Span>();
    if (references) {
        for (const auto& reference : *references) {
            if (reference.document == document) {
                result.push_back(reference.range);
            }
        }
    }
    std::ranges::sort(result);
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return {.analysis = std::move(analysis), .result = std::move(result)};
}

auto WorkspaceAnalysisSnapshot::workspace_symbols() const noexcept
    -> std::shared_ptr<const WorkspaceSymbolList> {
    return queries->symbols();
}

auto WorkspaceAnalysisSnapshot::semantic(
    std::span<const WorkspaceProjectModule> project,
    TimingOutput timings
) const noexcept -> WorkspaceSemanticQuery {
    auto resolved = queries->resolve(project, timings);
    return WorkspaceSemanticQuery {
        .result = resolved.node->result(timings),
        .documents = std::move(resolved.versions)
    };
}

auto WorkspaceAnalysisSnapshot::hover(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceHoverQuery {
    auto resolved = queries->navigation(project, document);
    if (!resolved) {
        return {.analysis = {.result = nullptr, .documents = {}}, .result = std::nullopt};
    }
    auto analysis = WorkspaceSemanticQuery {
        .result = resolved->node->source_result(),
        .documents = std::move(resolved->versions)
    };
    auto result = analysis.result->hover(document, offset);
    if (result) {
        if (const auto signature = callable_signature(*analysis.result, *result, true)) {
            result->description = signature->label;
        }
    }
    result = interop_hover(*analysis.result, document, offset, std::move(result));
    return WorkspaceHoverQuery {.analysis = std::move(analysis), .result = std::move(result)};
}

auto WorkspaceAnalysisSnapshot::definition(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceDefinitionQuery {
    auto resolved = queries->navigation(project, document);
    if (!resolved) {
        return {.analysis = {.result = nullptr, .documents = {}}, .result = std::nullopt};
    }
    auto analysis = WorkspaceSemanticQuery {
        .result = resolved->node->source_result(),
        .documents = std::move(resolved->versions)
    };
    const auto target = analysis.result->definition(document, offset);
    auto result = std::optional<WorkspaceVersionedLocation>();
    if (target) {
        if (const auto entry = queries->document(target->document)) {
            result.emplace(
                WorkspaceVersionedLocation {
                    .document = target->document,
                    .version = entry->version,
                    .range = target->range
                }
            );
        }
    }
    return WorkspaceDefinitionQuery {.analysis = std::move(analysis), .result = std::move(result)};
}

auto WorkspaceAnalysisSnapshot::source_semantic(
    std::span<const WorkspaceProjectModule> project
) const noexcept -> WorkspaceSemanticQuery {
    auto resolved = queries->resolve(project);
    return {.result = resolved.node->source_result(), .documents = std::move(resolved.versions)};
}

auto WorkspaceAnalysisSnapshot::references(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset,
    bool include_declaration
) const noexcept -> WorkspaceReferencesQuery {
    auto analysis = source_semantic(project);
    const auto locations = analysis.result->references(document, offset, include_declaration);
    auto result = std::optional<std::vector<WorkspaceVersionedLocation>>();
    if (locations) {
        result.emplace();
        for (const auto& location : *locations) {
            if (const auto entry = queries->document(location.document)) {
                result->push_back(
                    {.document = location.document,
                     .version = entry->version,
                     .range = location.range}
                );
            }
        }
    }
    return WorkspaceReferencesQuery {.analysis = std::move(analysis), .result = std::move(result)};
}

WorkspaceAnalysisHost::WorkspaceAnalysisHost(std::size_t semantic_cache_capacity) noexcept
    : queries(
          std::make_shared<WorkspaceQueries>(
              DocumentMap(),
              std::vector<std::shared_ptr<CachedAnalysis>>(),
              semantic_cache_capacity
          )
      ) {}

auto WorkspaceAnalysisHost::update(
    std::string document,
    std::int64_t version,
    std::string text
) noexcept -> std::expected<WorkspaceDocumentChange, WorkspaceDocumentUpdateFailure> {
    auto updates = std::vector<WorkspaceDocumentUpdate>();
    updates.push_back(
        {.document = std::move(document), .version = version, .text = std::move(text)}
    );
    auto result = update(std::move(updates));
    if (!result) {
        return std::unexpected(std::move(result.error().reason));
    }
    return result->front();
}

auto WorkspaceAnalysisHost::update(std::vector<WorkspaceDocumentUpdate> updates) noexcept
    -> std::expected<std::vector<WorkspaceDocumentChange>, WorkspaceDocumentBatchUpdateFailure> {
    if (updates.empty()) {
        return std::vector<WorkspaceDocumentChange>();
    }
    auto documents = std::set<std::string_view>();
    for (const auto& update : updates) {
        if (!documents.insert(update.document).second) {
            return std::unexpected(
                WorkspaceDocumentBatchUpdateFailure {
                    .document = update.document,
                    .reason = WorkspaceDuplicateDocumentUpdate {},
                }
            );
        }
        const auto previous = queries->document(update.document);
        if (previous && update.version <= previous->version) {
            return std::unexpected(
                WorkspaceDocumentBatchUpdateFailure {
                    .document = update.document,
                    .reason = WorkspaceStaleDocumentVersion {
                        .current = previous->version,
                        .received = update.version,
                    },
                }
            );
        }
    }
    auto inputs = queries->documents();
    auto changes = std::vector<WorkspaceDocumentChange>();
    changes.reserve(updates.size());
    for (auto& update : updates) {
        const auto previous = queries->document(update.document);
        auto change = WorkspaceDocumentChange::VersionOnly;
        auto next = previous ? previous->queries : nullptr;
        if (!previous || previous->queries->source().text != update.text) {
            auto source = SourceDocument::create(update.document, std::move(update.text));
            if (!source) {
                return std::unexpected(
                    WorkspaceDocumentBatchUpdateFailure {
                        .document = std::move(update.document),
                        .reason = std::move(source.error()),
                    }
                );
            }
            next = std::make_shared<DocumentQueries>(std::move(*source));
            change = previous ? WorkspaceDocumentChange::Changed : WorkspaceDocumentChange::Added;
        }
        inputs.insert_or_assign(
            std::move(update.document),
            DocumentEntry {.version = update.version, .queries = std::move(next)}
        );
        changes.push_back(change);
    }
    queries = std::make_shared<WorkspaceQueries>(
        std::move(inputs),
        queries->selections(),
        queries->capacity(),
        queries->completion_cache()
    );
    return changes;
}

auto WorkspaceAnalysisHost::remove(std::string_view document) noexcept -> bool {
    if (!queries->document(document)) {
        return false;
    }
    auto inputs = queries->documents();
    inputs.erase(inputs.find(document));
    queries = std::make_shared<WorkspaceQueries>(
        std::move(inputs),
        queries->selections(),
        queries->capacity(),
        queries->completion_cache()
    );
    return true;
}

auto WorkspaceAnalysisHost::snapshot() const noexcept -> WorkspaceAnalysisSnapshot {
    return WorkspaceAnalysisSnapshot(queries);
}

auto WorkspaceAnalysisSnapshot::completion(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceCompletionQuery {
    auto result = WorkspaceCompletionQuery {
        .replacement = Span::at(offset),
        .analysis = {},
        .candidates = {},
        .imports = {}
    };
    const auto entry = queries->document(document);
    if (!entry) {
        return result;
    }
    const auto source = entry->queries->source();
    if (offset > source.text.size()) {
        return result;
    }
    const auto parsed = entry->queries->syntax();
    for (const auto& token : parsed->tokens()) {
        if (token.span.start() <= offset
            && offset <= token.span.end()
            && token.kind == TokenKind::Identifier) {
            result.replacement = token.span;
            break;
        }
        if (token.span.start() <= offset
            && offset < token.span.end()
            && (token.span.start() < offset
                || token.kind == TokenKind::StringLiteral
                || token.kind == TokenKind::CharLiteral
                || token.kind == TokenKind::CStringLiteral
                || token.kind == TokenKind::InterpolationText
                || token.kind == TokenKind::CppSourceFragment)) {
            return result;
        }
    }
    if (const auto imported = source_import_completion(*parsed, offset)) {
        result.replacement = imported->replacement;
        if (!imported->accepts_name) {
            return result;
        }
        const auto owner = std::ranges::find(project, document, &WorkspaceProjectModule::document);
        if (owner == project.end()) {
            return result;
        }
        auto modules = std::set<CanonicalModulePath>();
        for (const auto& project_module : project) {
            if (project_module.module_path != owner->module_path) {
                modules.insert(project_module.module_path);
            }
        }
        for (const auto& provided : provided_exports()) {
            if (const auto path =
                    CanonicalModulePath::from_value(provided_module_path(provided.provider))) {
                modules.insert(*path);
            }
        }
        if (!imported->reference) {
            for (const auto& project_module : modules) {
                const auto reference = source_import_reference(
                    owner->module_path,
                    project_module,
                    imported->prefix.starts_with('.')
                );
                if (!reference.empty() && reference.starts_with(imported->prefix)) {
                    result.candidates.push_back(
                        {reference,
                         SourceCompletionKind::Module,
                         std::string(project_module.value())}
                    );
                }
            }
            return result;
        }
        const auto target =
            resolve_import_path(source.text, owner->module_path, *imported->reference);
        if (!target) {
            return result;
        }
        for (const auto& project_module : project) {
            if (project_module.module_path != *target) {
                continue;
            }
            if (const auto parsed = queries->document(project_module.document)) {
                result.candidates = source_import_candidates(
                    *parsed->queries->syntax(),
                    project_module.module_path,
                    owner->module_path
                );
            }
        }
        result.candidates.append_range(source_import_candidates(*target));
        std::erase_if(result.candidates, [&](const auto& candidate) noexcept {
            return std::ranges::contains(imported->selected, candidate.name);
        });
        return result;
    }
    constexpr auto marker = std::string_view("__carven_completion_query");
    auto text = std::string(source.text);
    text.replace(result.replacement.start(), result.replacement.size(), marker);
    auto temporary = SourceDocument::create(std::string(document), text);
    if (!temporary) {
        return result;
    }
    auto query_document = std::make_shared<DocumentQueries>(*temporary);
    if (!query_document->syntax()->recovered_syntax()) {
        return result;
    }
    auto documents = queries->documents();
    documents.insert_or_assign(
        std::string(document),
        DocumentEntry {.version = entry->version, .queries = std::move(query_document)}
    );
    auto private_queries = WorkspaceQueries(std::move(documents), {}, 0);
    const auto selection = private_queries.navigation_modules(project, document);
    if (!selection) {
        return result;
    }
    auto inputs = std::vector<WorkspaceSemanticInput>();
    for (const auto& project_module : *selection) {
        const auto input = private_queries.document(project_module.document);
        inputs.push_back(
            {.project_module = project_module,
             .source = input ? input->queries->source_owner() : nullptr,
             .parsed = input ? input->queries->syntax() : nullptr,
             .excluded = {}}
        );
    }
    const auto analysis = queries->complete(
        std::move(inputs),
        WorkspaceDocumentLocation {
            .document = std::string(document),
            .range = Span::at(result.replacement.start())
        }
    );
    result.analysis = analysis;
    for (const auto& candidate : analysis->completions()) {
        result.candidates.push_back(candidate);
    }
    std::ranges::sort(result.candidates, {}, &SourceCompletionCandidate::name);
    const auto context = analysis->completion_context();
    if (context == SourceCompletionContext::Name || context == SourceCompletionContext::Type) {
        result.imports = auto_imports(
            project,
            document,
            context == SourceCompletionContext::Type,
            result.candidates
        );
    }
    return result;
}

auto WorkspaceAnalysisSnapshot::prepare_rename(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceRenameQuery {
    auto resolved = queries->resolve(project);
    auto analysis =
        WorkspaceSemanticQuery {resolved.node->source_result(), std::move(resolved.versions)};
    auto result = analysis.result->prepare_rename(document, offset);
    if (result && std::ranges::none_of(project, [&](const auto& project_module) {
            return project_module.document == result->declaration.document;
        })) {
        result.reset();
    }
    return {std::move(analysis), std::move(result)};
}

auto WorkspaceAnalysisSnapshot::rename(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset,
    std::string_view new_name
) const noexcept -> std::expected<WorkspaceRenameQuery, WorkspaceRenameError> {
    if (!std::holds_alternative<OrdinaryIdentifier>(classify_identifier(new_name))
        || new_name == "ptr") {
        return std::unexpected(WorkspaceRenameError::InvalidName);
    }
    auto prepared = prepare_rename(project, document, offset);
    if (!prepared.result) {
        return prepared;
    }
    auto edits = std::map<std::string, std::vector<Span>, std::less<>>();
    for (const auto& location : prepared.result->references) {
        edits[location.document].push_back(location.range);
    }
    auto inputs = std::vector<WorkspaceSemanticInput>();
    for (const auto& project_module : project) {
        const auto entry = queries->document(project_module.document);
        if (!entry) {
            return std::unexpected(WorkspaceRenameError::Conflict);
        }
        auto source = entry->queries->source_owner();
        auto parsed = entry->queries->syntax();
        if (const auto found = edits.find(project_module.document); found != edits.end()) {
            auto text = std::string(source->source().text);
            for (const auto range : found->second | std::views::reverse) {
                text.replace(range.start(), range.size(), new_name);
            }
            auto replacement = SourceDocument::create(project_module.document, std::move(text));
            if (!replacement) {
                return std::unexpected(WorkspaceRenameError::Conflict);
            }
            source = std::move(*replacement);
            parsed = parse_document(source);
        }
        inputs.push_back(
            {.project_module = project_module,
             .source = std::move(source),
             .parsed = std::move(parsed),
             .excluded = {}}
        );
    }
    const auto remap = [&](std::string_view document, Span span) noexcept -> Span {
        const auto point = [&](std::uint32_t offset) {
            auto delta = std::int64_t(0);
            if (const auto found = edits.find(document); found != edits.end()) {
                for (const auto edit : found->second) {
                    if (edit.end() <= offset) {
                        delta += static_cast<std::int64_t>(new_name.size()) - edit.size();
                    }
                }
            }
            return static_cast<std::uint32_t>(offset + delta);
        };
        return Span::from_bounds(point(span.start()), point(span.end()));
    };
    const auto changed = analyze_workspace_source_queries(inputs);
    if (!changed->preserves_bindings(*prepared.analysis.result, remap)) {
        return std::unexpected(WorkspaceRenameError::Conflict);
    }
    return prepared;
}

auto WorkspaceAnalysisSnapshot::decorations(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    Span visible
) const noexcept -> WorkspaceDecorationsQuery {
    auto resolved = queries->navigation(project, document);
    if (!resolved) {
        return {{nullptr, {}}, {{}, {}}};
    }
    auto analysis =
        WorkspaceSemanticQuery {resolved->node->source_result(), std::move(resolved->versions)};
    auto result =
        collect_workspace_decorations(*this, project, document, visible, *analysis.result);
    return {std::move(analysis), std::move(result)};
}
