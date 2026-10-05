module carven:editor.analysis.impl;

import :editor.analysis;
import :editor.document;
import :editor.semantic;
import :editor.symbols;
import :source.text;
import std;

namespace editor {

class DocumentQueries final {
public:
    DocumentQueries(
        std::shared_ptr<const DocumentSource> source,
        std::shared_ptr<const DocumentSymbolList> previous_symbols,
        std::shared_ptr<QueryCounts> counts
    ) noexcept;
    auto source() const noexcept -> SourceView;
    auto source_owner() const noexcept -> std::shared_ptr<const DocumentSource>;
    auto syntax() noexcept -> std::shared_ptr<const DocumentSyntax>;
    auto symbols() noexcept -> std::shared_ptr<const DocumentSymbolList>;
    auto symbol_baseline() const noexcept -> std::shared_ptr<const DocumentSymbolList>;

private:
    std::shared_ptr<const DocumentSource> input;
    std::shared_ptr<const DocumentSymbolList> previous_symbols;
    std::shared_ptr<QueryCounts> work_counts;
    std::shared_ptr<const DocumentSyntax> cached_syntax;
    std::shared_ptr<const DocumentSymbolList> cached_symbols;
    bool symbols_computed = false;
};

struct DocumentEntry final {
    std::int64_t version;
    std::shared_ptr<DocumentQueries> queries;
};

using DocumentMap = std::map<std::string, DocumentEntry, std::less<>>;
using SymbolDependencies =
    std::vector<std::pair<std::string, std::shared_ptr<const DocumentSymbolList>>>;

struct WorkspaceIndex final {
    SymbolDependencies dependencies;
    std::shared_ptr<const WorkspaceSymbolList> result;
};

class CachedProject final {
public:
    CachedProject(std::vector<SemanticInput> inputs, std::shared_ptr<QueryCounts> counts) noexcept;
    auto matches_documents(const DocumentMap& documents) const noexcept -> bool;
    auto result() noexcept -> std::shared_ptr<const SemanticAnalysis>;

private:
    std::vector<SemanticInput> inputs;
    std::shared_ptr<QueryCounts> work_counts;
    std::shared_ptr<const SemanticAnalysis> cached_result;
};

using ProjectKey = std::vector<std::pair<std::string, std::string>>;
using ProjectCache = std::map<ProjectKey, std::shared_ptr<CachedProject>>;

struct ResolvedProject final {
    std::shared_ptr<CachedProject> node;
    std::vector<DocumentVersion> versions;
};

struct RecentRequest final {
    std::vector<ProjectModule> modules;
    ResolvedProject result;
};

class WorkspaceQueries final {
public:
    WorkspaceQueries(
        DocumentMap inputs,
        std::shared_ptr<QueryCounts> counts,
        std::shared_ptr<const WorkspaceIndex> previous_index,
        ProjectCache project_cache
    ) noexcept;
    auto document(std::string_view key) const noexcept -> std::optional<DocumentEntry>;
    auto documents() const noexcept -> DocumentMap;
    auto counts() const noexcept -> std::shared_ptr<QueryCounts>;
    auto index_baseline() const noexcept -> std::shared_ptr<const WorkspaceIndex>;
    auto symbols() noexcept -> std::shared_ptr<const WorkspaceSymbolList>;
    auto semantic(std::span<const ProjectModule> project) noexcept -> ResolvedProject;
    auto project_cache() const noexcept -> ProjectCache;

private:
    DocumentMap inputs;
    std::shared_ptr<QueryCounts> work_counts;
    std::shared_ptr<const WorkspaceIndex> previous_index;
    std::shared_ptr<const WorkspaceIndex> cached_index;
    ProjectCache cached_projects;
    // A single recent request avoids canonical sorting on repeated UI queries.
    std::optional<RecentRequest> recent;
};

DocumentQueries::DocumentQueries(
    std::shared_ptr<const DocumentSource> source,
    std::shared_ptr<const DocumentSymbolList> previous_symbols,
    std::shared_ptr<QueryCounts> counts
) noexcept
    : input(std::move(source)),
      previous_symbols(std::move(previous_symbols)),
      work_counts(std::move(counts)) {}

auto DocumentQueries::source() const noexcept -> SourceView {
    return input->source();
}

auto DocumentQueries::source_owner() const noexcept -> std::shared_ptr<const DocumentSource> {
    return input;
}

auto DocumentQueries::syntax() noexcept -> std::shared_ptr<const DocumentSyntax> {
    if (!cached_syntax) {
        ++work_counts->syntax;
        cached_syntax = parse_document(input);
    }
    return cached_syntax;
}

auto DocumentQueries::symbols() noexcept -> std::shared_ptr<const DocumentSymbolList> {
    if (symbols_computed) {
        return cached_symbols;
    }
    symbols_computed = true;
    const auto parsed = syntax();
    if (parsed->recovered_syntax()) {
        ++work_counts->document_symbols;
        auto result = collect_symbols(*parsed);
        if (previous_symbols && *previous_symbols == result) {
            cached_symbols = previous_symbols;
        } else {
            cached_symbols = std::make_shared<const DocumentSymbolList>(std::move(result));
        }
    }
    previous_symbols.reset();
    return cached_symbols;
}

auto DocumentQueries::symbol_baseline() const noexcept
    -> std::shared_ptr<const DocumentSymbolList> {
    return symbols_computed ? cached_symbols : previous_symbols;
}

WorkspaceQueries::WorkspaceQueries(
    DocumentMap inputs,
    std::shared_ptr<QueryCounts> counts,
    std::shared_ptr<const WorkspaceIndex> previous_index,
    ProjectCache project_cache
) noexcept
    : inputs(std::move(inputs)),
      work_counts(std::move(counts)),
      previous_index(std::move(previous_index)),
      cached_projects(std::move(project_cache)) {
    // Inherit only content-valid nodes. Old snapshots keep their own nodes;
    // abandoned requests must not keep removed or superseded sources alive.
    std::erase_if(cached_projects, [this](const auto& entry) noexcept {
        return !entry.second->matches_documents(this->inputs);
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

auto WorkspaceQueries::counts() const noexcept -> std::shared_ptr<QueryCounts> {
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
    std::vector<SemanticInput> inputs,
    std::shared_ptr<QueryCounts> counts
) noexcept
    : inputs(std::move(inputs)),
      work_counts(std::move(counts)) {}

auto CachedProject::matches_documents(const DocumentMap& documents) const noexcept -> bool {
    return std::ranges::all_of(inputs, [&](const SemanticInput& input) noexcept {
        const auto found = documents.find(input.module.document);
        const auto source =
            found == documents.end() ? nullptr : found->second.queries->source_owner();
        return input.source == source;
    });
}

auto CachedProject::result() noexcept -> std::shared_ptr<const SemanticAnalysis> {
    if (!cached_result) {
        ++work_counts->semantic;
        cached_result = analyze_project(inputs);
    }
    return cached_result;
}

auto WorkspaceQueries::semantic(std::span<const ProjectModule> project) noexcept
    -> ResolvedProject {
    if (recent && std::ranges::equal(project, recent->modules)) {
        return recent->result;
    }
    auto versions = std::map<std::string, std::int64_t>();
    for (const auto& module : project) {
        if (const auto entry = document(module.document)) {
            versions.emplace(module.document, entry->version);
        }
    }
    auto document_versions = std::vector<DocumentVersion>();
    for (const auto& [document, version] : versions) {
        document_versions.push_back({.document = document, .version = version});
    }
    auto modules = std::vector<ProjectModule>(project.begin(), project.end());
    std::ranges::sort(
        modules,
        [](const ProjectModule& left, const ProjectModule& right) static noexcept {
            return std::pair(left.module_path.value(), std::string_view(left.document))
                < std::pair(right.module_path.value(), std::string_view(right.document));
        }
    );
    auto key = ProjectKey();
    for (const auto& module : modules) {
        key.emplace_back(module.document, module.module_path.value());
    }
    auto node = std::shared_ptr<CachedProject>();
    const auto previous = cached_projects.find(key);
    if (previous != cached_projects.end()) {
        node = previous->second;
    } else {
        auto resolved = std::vector<SemanticInput>();
        for (const auto& module : modules) {
            const auto entry = document(module.document);
            resolved.push_back(
                {.module = module, .source = entry ? entry->queries->source_owner() : nullptr}
            );
        }
        node = std::make_shared<CachedProject>(std::move(resolved), work_counts);
        cached_projects.emplace(std::move(key), node);
    }
    recent.emplace(
        RecentRequest {
            .modules = std::vector<ProjectModule>(project.begin(), project.end()),
            .result =
                ResolvedProject {.node = std::move(node), .versions = std::move(document_versions)}
        }
    );
    return recent->result;
}

auto WorkspaceQueries::project_cache() const noexcept -> ProjectCache {
    return cached_projects;
}

Analysis::Analysis(std::shared_ptr<WorkspaceQueries> queries) noexcept
    : queries(std::move(queries)) {}

auto Analysis::syntax(std::string_view document) const noexcept -> std::optional<SyntaxQuery> {
    const auto entry = queries->document(document);
    if (!entry) {
        return std::nullopt;
    }
    return SyntaxQuery {.version = entry->version, .result = entry->queries->syntax()};
}

auto Analysis::document_symbols(std::string_view document) const noexcept
    -> std::optional<SymbolsQuery> {
    const auto entry = queries->document(document);
    if (!entry) {
        return std::nullopt;
    }
    return SymbolsQuery {
        .document = SyntaxQuery {.version = entry->version, .result = entry->queries->syntax()},
        .result = entry->queries->symbols(),
    };
}

auto Analysis::workspace_symbols() const noexcept -> std::shared_ptr<const WorkspaceSymbolList> {
    return queries->symbols();
}

auto Analysis::semantic(std::span<const ProjectModule> project) const noexcept -> SemanticQuery {
    auto resolved = queries->semantic(project);
    return SemanticQuery {
        .result = resolved.node->result(),
        .documents = std::move(resolved.versions)
    };
}

auto Analysis::hover(
    std::span<const ProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> HoverQuery {
    auto analysis = semantic(project);
    const auto result = analysis.result->hover(document, offset);
    return HoverQuery {.analysis = std::move(analysis), .result = result};
}

auto Analysis::definition(
    std::span<const ProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> DefinitionQuery {
    auto analysis = semantic(project);
    const auto target = analysis.result->definition(document, offset);
    auto result = std::optional<VersionedLocation>();
    if (target) {
        if (const auto entry = queries->document(target->document)) {
            result.emplace(
                VersionedLocation {
                    .document = target->document,
                    .version = entry->version,
                    .range = target->range
                }
            );
        }
    }
    return DefinitionQuery {.analysis = std::move(analysis), .result = std::move(result)};
}

auto Analysis::references(
    std::span<const ProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> ReferencesQuery {
    auto analysis = semantic(project);
    const auto locations = analysis.result->references(document, offset);
    auto result = std::optional<std::vector<VersionedLocation>>();
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
    return ReferencesQuery {.analysis = std::move(analysis), .result = std::move(result)};
}

auto Analysis::counts() const noexcept -> QueryCounts {
    return *queries->counts();
}

AnalysisHost::AnalysisHost() noexcept
    : queries(
          std::make_shared<WorkspaceQueries>(
              DocumentMap(),
              std::make_shared<QueryCounts>(QueryCounts {
                  .syntax = 0uz,
                  .document_symbols = 0uz,
                  .workspace_symbols = 0uz,
                  .semantic = 0uz
              }),
              nullptr,
              ProjectCache()
          )
      ) {}

auto AnalysisHost::update(std::string document, std::int64_t version, std::string text) noexcept
    -> std::expected<DocumentChange, DocumentUpdateFailure> {
    const auto previous = queries->document(document);
    if (previous && version <= previous->version) {
        return std::unexpected(DocumentUpdateFailure(
            StaleDocumentVersion {
                .current = previous->version,
                .received = version,
            }
        ));
    }
    auto inputs = queries->documents();
    auto change = DocumentChange::VersionOnly;
    if (previous && previous->queries->source().text == text) {
        inputs.insert_or_assign(
            document,
            DocumentEntry {.version = version, .queries = previous->queries}
        );
    } else {
        auto source = DocumentSource::create(document, std::move(text));
        if (!source) {
            return std::unexpected(DocumentUpdateFailure(std::move(source.error())));
        }
        auto next = std::make_shared<DocumentQueries>(
            std::move(*source),
            previous ? previous->queries->symbol_baseline() : nullptr,
            queries->counts()
        );
        inputs.insert_or_assign(
            document,
            DocumentEntry {.version = version, .queries = std::move(next)}
        );
        change = previous ? DocumentChange::Changed : DocumentChange::Added;
    }
    queries = std::make_shared<WorkspaceQueries>(
        std::move(inputs),
        queries->counts(),
        queries->index_baseline(),
        queries->project_cache()
    );
    return change;
}

auto AnalysisHost::remove(std::string_view document) noexcept -> bool {
    if (!queries->document(document)) {
        return false;
    }
    auto inputs = queries->documents();
    inputs.erase(inputs.find(document));
    queries = std::make_shared<WorkspaceQueries>(
        std::move(inputs),
        queries->counts(),
        queries->index_baseline(),
        queries->project_cache()
    );
    return true;
}

auto AnalysisHost::snapshot() const noexcept -> Analysis {
    return Analysis(queries);
}

} // namespace editor
