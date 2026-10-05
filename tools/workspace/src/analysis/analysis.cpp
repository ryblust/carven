module carven:workspace.analysis.impl;

import :source.text;
import :workspace.analysis;
import :workspace.document;
import :workspace.semantic;
import :workspace.symbols;
import std;

namespace {

class DocumentQueries final {
public:
    DocumentQueries(
        std::shared_ptr<const WorkspaceDocumentSource> source,
        std::shared_ptr<const WorkspaceDocumentSymbolList> previous_symbols,
        std::shared_ptr<WorkspaceQueryCounts> counts
    ) noexcept;
    auto source() const noexcept -> SourceView;
    auto source_owner() const noexcept -> std::shared_ptr<const WorkspaceDocumentSource>;
    auto syntax() noexcept -> std::shared_ptr<const WorkspaceDocumentSyntax>;
    auto symbols() noexcept -> std::shared_ptr<const WorkspaceDocumentSymbolList>;
    auto symbol_baseline() const noexcept -> std::shared_ptr<const WorkspaceDocumentSymbolList>;

private:
    std::shared_ptr<const WorkspaceDocumentSource> input;
    std::shared_ptr<const WorkspaceDocumentSymbolList> previous_symbols;
    std::shared_ptr<WorkspaceQueryCounts> work_counts;
    std::shared_ptr<const WorkspaceDocumentSyntax> cached_syntax;
    std::shared_ptr<const WorkspaceDocumentSymbolList> cached_symbols;
    bool symbols_computed = false;
};

struct DocumentEntry final {
    std::int64_t version;
    std::shared_ptr<DocumentQueries> queries;
};

using DocumentMap = std::map<std::string, DocumentEntry, std::less<>>;
using SymbolDependencies =
    std::vector<std::pair<std::string, std::shared_ptr<const WorkspaceDocumentSymbolList>>>;

struct WorkspaceIndex final {
    SymbolDependencies dependencies;
    std::shared_ptr<const WorkspaceSymbolList> result;
};

class CachedProject final {
public:
    CachedProject(
        std::vector<WorkspaceSemanticInput> inputs,
        std::shared_ptr<WorkspaceQueryCounts> counts
    ) noexcept;
    auto matches_documents(const DocumentMap& documents) const noexcept -> bool;
    auto matches_modules(std::span<const WorkspaceProjectModule> modules) const noexcept -> bool;
    auto result() noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;

private:
    std::vector<WorkspaceSemanticInput> inputs;
    std::shared_ptr<WorkspaceQueryCounts> work_counts;
    std::shared_ptr<const WorkspaceSemanticAnalysis> cached_result;
};

} // namespace

class WorkspaceQueries final {
    struct ResolvedProject final {
        std::shared_ptr<CachedProject> node;
        std::vector<WorkspaceDocumentVersion> versions;
    };

    struct RecentRequest final {
        std::vector<WorkspaceProjectModule> modules;
        std::vector<WorkspaceDocumentVersion> versions;
    };

public:
    WorkspaceQueries(
        DocumentMap inputs,
        std::shared_ptr<WorkspaceQueryCounts> counts,
        std::shared_ptr<const WorkspaceIndex> previous_index,
        std::shared_ptr<CachedProject> cached_project
    ) noexcept;
    auto document(std::string_view key) const noexcept -> std::optional<DocumentEntry>;
    auto documents() const noexcept -> DocumentMap;
    auto counts() const noexcept -> std::shared_ptr<WorkspaceQueryCounts>;
    auto index_baseline() const noexcept -> std::shared_ptr<const WorkspaceIndex>;
    auto symbols() noexcept -> std::shared_ptr<const WorkspaceSymbolList>;
    auto semantic(std::span<const WorkspaceProjectModule> project) noexcept -> ResolvedProject;
    auto project_baseline() const noexcept -> std::shared_ptr<CachedProject>;

private:
    DocumentMap inputs;
    std::shared_ptr<WorkspaceQueryCounts> work_counts;
    std::shared_ptr<const WorkspaceIndex> previous_index;
    std::shared_ptr<const WorkspaceIndex> cached_index;
    std::shared_ptr<CachedProject> cached_project;
    // A single recent request avoids canonical sorting on repeated UI queries.
    std::optional<RecentRequest> recent;
};

DocumentQueries::DocumentQueries(
    std::shared_ptr<const WorkspaceDocumentSource> source,
    std::shared_ptr<const WorkspaceDocumentSymbolList> previous_symbols,
    std::shared_ptr<WorkspaceQueryCounts> counts
) noexcept
    : input(std::move(source)),
      previous_symbols(std::move(previous_symbols)),
      work_counts(std::move(counts)) {}

auto DocumentQueries::source() const noexcept -> SourceView {
    return input->source();
}

auto DocumentQueries::source_owner() const noexcept
    -> std::shared_ptr<const WorkspaceDocumentSource> {
    return input;
}

auto DocumentQueries::syntax() noexcept -> std::shared_ptr<const WorkspaceDocumentSyntax> {
    if (!cached_syntax) {
        ++work_counts->syntax;
        cached_syntax = parse_workspace_document(input);
    }
    return cached_syntax;
}

auto DocumentQueries::symbols() noexcept -> std::shared_ptr<const WorkspaceDocumentSymbolList> {
    if (symbols_computed) {
        return cached_symbols;
    }
    symbols_computed = true;
    const auto parsed = syntax();
    if (parsed->recovered_syntax()) {
        ++work_counts->document_symbols;
        auto result = collect_workspace_document_symbols(*parsed);
        if (previous_symbols && *previous_symbols == result) {
            cached_symbols = previous_symbols;
        } else {
            cached_symbols = std::make_shared<const WorkspaceDocumentSymbolList>(std::move(result));
        }
    }
    previous_symbols.reset();
    return cached_symbols;
}

auto DocumentQueries::symbol_baseline() const noexcept
    -> std::shared_ptr<const WorkspaceDocumentSymbolList> {
    return symbols_computed ? cached_symbols : previous_symbols;
}

WorkspaceQueries::WorkspaceQueries(
    DocumentMap inputs,
    std::shared_ptr<WorkspaceQueryCounts> counts,
    std::shared_ptr<const WorkspaceIndex> previous_index,
    std::shared_ptr<CachedProject> cached_project
) noexcept
    : inputs(std::move(inputs)),
      work_counts(std::move(counts)),
      previous_index(std::move(previous_index)),
      cached_project(std::move(cached_project)) {
    // Old snapshots keep their nodes; this snapshot inherits only valid inputs.
    if (this->cached_project && !this->cached_project->matches_documents(this->inputs)) {
        this->cached_project.reset();
    }
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

auto WorkspaceQueries::counts() const noexcept -> std::shared_ptr<WorkspaceQueryCounts> {
    return work_counts;
}

auto WorkspaceQueries::index_baseline() const noexcept -> std::shared_ptr<const WorkspaceIndex> {
    return cached_index ? cached_index : previous_index;
}

auto WorkspaceQueries::symbols() noexcept -> std::shared_ptr<const WorkspaceSymbolList> {
    if (cached_index) {
        return cached_index->result;
    }
    auto dependencies = SymbolDependencies();
    for (const auto& [key, entry] : inputs) {
        dependencies.emplace_back(key, entry.queries->symbols());
    }
    if (previous_index && previous_index->dependencies == dependencies) {
        cached_index = previous_index;
    } else {
        ++work_counts->workspace_symbols;
        auto result = WorkspaceSymbolList();
        for (const auto& [key, symbols] : dependencies) {
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
        cached_index = std::make_shared<const WorkspaceIndex>(WorkspaceIndex {
            .dependencies = std::move(dependencies),
            .result = std::make_shared<const WorkspaceSymbolList>(std::move(result)),
        });
    }
    previous_index.reset();
    return cached_index->result;
}

CachedProject::CachedProject(
    std::vector<WorkspaceSemanticInput> inputs,
    std::shared_ptr<WorkspaceQueryCounts> counts
) noexcept
    : inputs(std::move(inputs)),
      work_counts(std::move(counts)) {}

auto CachedProject::matches_documents(const DocumentMap& documents) const noexcept -> bool {
    return std::ranges::all_of(inputs, [&](const WorkspaceSemanticInput& input) noexcept {
        const auto found = documents.find(input.module.document);
        const auto source =
            found == documents.end() ? nullptr : found->second.queries->source_owner();
        return input.source == source;
    });
}

auto CachedProject::matches_modules(std::span<const WorkspaceProjectModule> modules) const noexcept
    -> bool {
    return std::ranges::equal(
        inputs,
        modules,
        [](const WorkspaceSemanticInput& input,
           const WorkspaceProjectModule& module) static noexcept { return input.module == module; }
    );
}

auto CachedProject::result() noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis> {
    if (!cached_result) {
        ++work_counts->semantic;
        cached_result = analyze_workspace_project(inputs);
    }
    return cached_result;
}

auto WorkspaceQueries::semantic(std::span<const WorkspaceProjectModule> project) noexcept
    -> ResolvedProject {
    if (recent && std::ranges::equal(project, recent->modules)) {
        return {.node = cached_project, .versions = recent->versions};
    }
    auto versions = std::map<std::string, std::int64_t>();
    for (const auto& module : project) {
        if (const auto entry = document(module.document)) {
            versions.emplace(module.document, entry->version);
        }
    }
    auto document_versions = std::vector<WorkspaceDocumentVersion>();
    for (const auto& [document, version] : versions) {
        document_versions.push_back({.document = document, .version = version});
    }
    auto modules = std::vector<WorkspaceProjectModule>(project.begin(), project.end());
    std::ranges::sort(
        modules,
        [](const WorkspaceProjectModule& left,
           const WorkspaceProjectModule& right) static noexcept {
            return std::pair(left.module_path.value(), std::string_view(left.document))
                < std::pair(right.module_path.value(), std::string_view(right.document));
        }
    );
    if (!cached_project || !cached_project->matches_modules(modules)) {
        auto resolved = std::vector<WorkspaceSemanticInput>();
        for (const auto& module : modules) {
            const auto entry = document(module.document);
            resolved.push_back(
                {.module = module, .source = entry ? entry->queries->source_owner() : nullptr}
            );
        }
        cached_project = std::make_shared<CachedProject>(std::move(resolved), work_counts);
    }
    recent.emplace(
        RecentRequest {
            .modules = std::vector<WorkspaceProjectModule>(project.begin(), project.end()),
            .versions = std::move(document_versions)
        }
    );
    return {.node = cached_project, .versions = recent->versions};
}

auto WorkspaceQueries::project_baseline() const noexcept -> std::shared_ptr<CachedProject> {
    return cached_project;
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

auto WorkspaceAnalysisSnapshot::workspace_symbols() const noexcept
    -> std::shared_ptr<const WorkspaceSymbolList> {
    return queries->symbols();
}

auto WorkspaceAnalysisSnapshot::semantic(
    std::span<const WorkspaceProjectModule> project
) const noexcept -> WorkspaceSemanticQuery {
    auto resolved = queries->semantic(project);
    return WorkspaceSemanticQuery {
        .result = resolved.node->result(),
        .documents = std::move(resolved.versions)
    };
}

auto WorkspaceAnalysisSnapshot::hover(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceHoverQuery {
    auto analysis = semantic(project);
    const auto result = analysis.result->hover(document, offset);
    return WorkspaceHoverQuery {.analysis = std::move(analysis), .result = result};
}

auto WorkspaceAnalysisSnapshot::definition(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceDefinitionQuery {
    auto analysis = semantic(project);
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

auto WorkspaceAnalysisSnapshot::references(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> WorkspaceReferencesQuery {
    auto analysis = semantic(project);
    const auto locations = analysis.result->references(document, offset);
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

auto WorkspaceAnalysisSnapshot::counts() const noexcept -> WorkspaceQueryCounts {
    return *queries->counts();
}

WorkspaceAnalysisHost::WorkspaceAnalysisHost() noexcept
    : queries(
          std::make_shared<WorkspaceQueries>(
              DocumentMap(),
              std::make_shared<WorkspaceQueryCounts>(WorkspaceQueryCounts {
                  .syntax = 0uz,
                  .document_symbols = 0uz,
                  .workspace_symbols = 0uz,
                  .semantic = 0uz
              }),
              nullptr,
              nullptr
          )
      ) {}

auto WorkspaceAnalysisHost::update(
    std::string document,
    std::int64_t version,
    std::string text
) noexcept -> std::expected<WorkspaceDocumentChange, WorkspaceDocumentUpdateFailure> {
    const auto previous = queries->document(document);
    if (previous && version <= previous->version) {
        return std::unexpected(WorkspaceDocumentUpdateFailure(
            WorkspaceStaleDocumentVersion {
                .current = previous->version,
                .received = version,
            }
        ));
    }
    auto inputs = queries->documents();
    auto change = WorkspaceDocumentChange::VersionOnly;
    if (previous && previous->queries->source().text == text) {
        inputs.insert_or_assign(
            std::move(document),
            DocumentEntry {.version = version, .queries = previous->queries}
        );
    } else {
        auto source = WorkspaceDocumentSource::create(document, std::move(text));
        if (!source) {
            return std::unexpected(WorkspaceDocumentUpdateFailure(std::move(source.error())));
        }
        auto next = std::make_shared<DocumentQueries>(
            std::move(*source),
            previous ? previous->queries->symbol_baseline() : nullptr,
            queries->counts()
        );
        inputs.insert_or_assign(
            std::move(document),
            DocumentEntry {.version = version, .queries = std::move(next)}
        );
        change = previous ? WorkspaceDocumentChange::Changed : WorkspaceDocumentChange::Added;
    }
    queries = std::make_shared<WorkspaceQueries>(
        std::move(inputs),
        queries->counts(),
        queries->index_baseline(),
        queries->project_baseline()
    );
    return change;
}

auto WorkspaceAnalysisHost::remove(std::string_view document) noexcept -> bool {
    if (!queries->document(document)) {
        return false;
    }
    auto inputs = queries->documents();
    inputs.erase(inputs.find(document));
    queries = std::make_shared<WorkspaceQueries>(
        std::move(inputs),
        queries->counts(),
        queries->index_baseline(),
        queries->project_baseline()
    );
    return true;
}

auto WorkspaceAnalysisHost::snapshot() const noexcept -> WorkspaceAnalysisSnapshot {
    return WorkspaceAnalysisSnapshot(queries);
}
