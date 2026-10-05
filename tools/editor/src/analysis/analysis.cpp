module carven:editor.analysis.impl;

import :editor.analysis;
import :editor.document;
import :editor.semantic;
import :editor.symbols;
import :source.text;
import std;

namespace {

class DocumentQueries final {
public:
    DocumentQueries(
        std::shared_ptr<const EditorDocumentSource> source,
        std::shared_ptr<const EditorDocumentSymbolList> previous_symbols,
        std::shared_ptr<EditorQueryCounts> counts
    ) noexcept;
    auto source() const noexcept -> SourceView;
    auto source_owner() const noexcept -> std::shared_ptr<const EditorDocumentSource>;
    auto syntax() noexcept -> std::shared_ptr<const EditorDocumentSyntax>;
    auto symbols() noexcept -> std::shared_ptr<const EditorDocumentSymbolList>;
    auto symbol_baseline() const noexcept -> std::shared_ptr<const EditorDocumentSymbolList>;

private:
    std::shared_ptr<const EditorDocumentSource> input;
    std::shared_ptr<const EditorDocumentSymbolList> previous_symbols;
    std::shared_ptr<EditorQueryCounts> work_counts;
    std::shared_ptr<const EditorDocumentSyntax> cached_syntax;
    std::shared_ptr<const EditorDocumentSymbolList> cached_symbols;
    bool symbols_computed = false;
};

struct DocumentEntry final {
    std::int64_t version;
    std::shared_ptr<DocumentQueries> queries;
};

using DocumentMap = std::map<std::string, DocumentEntry, std::less<>>;
using SymbolDependencies =
    std::vector<std::pair<std::string, std::shared_ptr<const EditorDocumentSymbolList>>>;

struct WorkspaceIndex final {
    SymbolDependencies dependencies;
    std::shared_ptr<const EditorWorkspaceSymbolList> result;
};

class CachedProject final {
public:
    CachedProject(
        std::vector<EditorSemanticInput> inputs,
        std::shared_ptr<EditorQueryCounts> counts
    ) noexcept;
    auto matches_documents(const DocumentMap& documents) const noexcept -> bool;
    auto result() noexcept -> std::shared_ptr<const EditorSemanticAnalysis>;

private:
    std::vector<EditorSemanticInput> inputs;
    std::shared_ptr<EditorQueryCounts> work_counts;
    std::shared_ptr<const EditorSemanticAnalysis> cached_result;
};

using ProjectKey = std::vector<std::pair<std::string, std::string>>;
using ProjectCache = std::map<ProjectKey, std::shared_ptr<CachedProject>>;

} // namespace

class EditorWorkspaceQueries final {
    struct ResolvedProject final {
        std::shared_ptr<CachedProject> node;
        std::vector<EditorDocumentVersion> versions;
    };

    struct RecentRequest final {
        std::vector<EditorProjectModule> modules;
        ResolvedProject result;
    };

public:
    EditorWorkspaceQueries(
        DocumentMap inputs,
        std::shared_ptr<EditorQueryCounts> counts,
        std::shared_ptr<const WorkspaceIndex> previous_index,
        ProjectCache project_cache
    ) noexcept;
    auto document(std::string_view key) const noexcept -> std::optional<DocumentEntry>;
    auto documents() const noexcept -> DocumentMap;
    auto counts() const noexcept -> std::shared_ptr<EditorQueryCounts>;
    auto index_baseline() const noexcept -> std::shared_ptr<const WorkspaceIndex>;
    auto symbols() noexcept -> std::shared_ptr<const EditorWorkspaceSymbolList>;
    auto semantic(std::span<const EditorProjectModule> project) noexcept -> ResolvedProject;
    auto project_cache() const noexcept -> ProjectCache;

private:
    DocumentMap inputs;
    std::shared_ptr<EditorQueryCounts> work_counts;
    std::shared_ptr<const WorkspaceIndex> previous_index;
    std::shared_ptr<const WorkspaceIndex> cached_index;
    ProjectCache cached_projects;
    // A single recent request avoids canonical sorting on repeated UI queries.
    std::optional<RecentRequest> recent;
};

DocumentQueries::DocumentQueries(
    std::shared_ptr<const EditorDocumentSource> source,
    std::shared_ptr<const EditorDocumentSymbolList> previous_symbols,
    std::shared_ptr<EditorQueryCounts> counts
) noexcept
    : input(std::move(source)),
      previous_symbols(std::move(previous_symbols)),
      work_counts(std::move(counts)) {}

auto DocumentQueries::source() const noexcept -> SourceView {
    return input->source();
}

auto DocumentQueries::source_owner() const noexcept -> std::shared_ptr<const EditorDocumentSource> {
    return input;
}

auto DocumentQueries::syntax() noexcept -> std::shared_ptr<const EditorDocumentSyntax> {
    if (!cached_syntax) {
        ++work_counts->syntax;
        cached_syntax = parse_editor_document(input);
    }
    return cached_syntax;
}

auto DocumentQueries::symbols() noexcept -> std::shared_ptr<const EditorDocumentSymbolList> {
    if (symbols_computed) {
        return cached_symbols;
    }
    symbols_computed = true;
    const auto parsed = syntax();
    if (parsed->recovered_syntax()) {
        ++work_counts->document_symbols;
        auto result = collect_editor_symbols(*parsed);
        if (previous_symbols && *previous_symbols == result) {
            cached_symbols = previous_symbols;
        } else {
            cached_symbols = std::make_shared<const EditorDocumentSymbolList>(std::move(result));
        }
    }
    previous_symbols.reset();
    return cached_symbols;
}

auto DocumentQueries::symbol_baseline() const noexcept
    -> std::shared_ptr<const EditorDocumentSymbolList> {
    return symbols_computed ? cached_symbols : previous_symbols;
}

EditorWorkspaceQueries::EditorWorkspaceQueries(
    DocumentMap inputs,
    std::shared_ptr<EditorQueryCounts> counts,
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

auto EditorWorkspaceQueries::document(std::string_view key) const noexcept
    -> std::optional<DocumentEntry> {
    const auto found = inputs.find(key);
    if (found == inputs.end()) {
        return std::nullopt;
    }
    return found->second;
}

auto EditorWorkspaceQueries::documents() const noexcept -> DocumentMap {
    return inputs;
}

auto EditorWorkspaceQueries::counts() const noexcept -> std::shared_ptr<EditorQueryCounts> {
    return work_counts;
}

auto EditorWorkspaceQueries::index_baseline() const noexcept
    -> std::shared_ptr<const WorkspaceIndex> {
    return cached_index ? cached_index : previous_index;
}

auto EditorWorkspaceQueries::symbols() noexcept
    -> std::shared_ptr<const EditorWorkspaceSymbolList> {
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
        auto result = EditorWorkspaceSymbolList();
        for (const auto& [key, symbols] : dependencies) {
            if (!symbols) {
                continue;
            }
            for (const auto& symbol : *symbols) {
                result.push_back(
                    EditorWorkspaceSymbol {
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
            .result = std::make_shared<const EditorWorkspaceSymbolList>(std::move(result)),
        });
    }
    previous_index.reset();
    return cached_index->result;
}

CachedProject::CachedProject(
    std::vector<EditorSemanticInput> inputs,
    std::shared_ptr<EditorQueryCounts> counts
) noexcept
    : inputs(std::move(inputs)),
      work_counts(std::move(counts)) {}

auto CachedProject::matches_documents(const DocumentMap& documents) const noexcept -> bool {
    return std::ranges::all_of(inputs, [&](const EditorSemanticInput& input) noexcept {
        const auto found = documents.find(input.module.document);
        const auto source =
            found == documents.end() ? nullptr : found->second.queries->source_owner();
        return input.source == source;
    });
}

auto CachedProject::result() noexcept -> std::shared_ptr<const EditorSemanticAnalysis> {
    if (!cached_result) {
        ++work_counts->semantic;
        cached_result = analyze_editor_project(inputs);
    }
    return cached_result;
}

auto EditorWorkspaceQueries::semantic(std::span<const EditorProjectModule> project) noexcept
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
    auto document_versions = std::vector<EditorDocumentVersion>();
    for (const auto& [document, version] : versions) {
        document_versions.push_back({.document = document, .version = version});
    }
    auto modules = std::vector<EditorProjectModule>(project.begin(), project.end());
    std::ranges::sort(
        modules,
        [](const EditorProjectModule& left, const EditorProjectModule& right) static noexcept {
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
        auto resolved = std::vector<EditorSemanticInput>();
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
            .modules = std::vector<EditorProjectModule>(project.begin(), project.end()),
            .result =
                ResolvedProject {.node = std::move(node), .versions = std::move(document_versions)}
        }
    );
    return recent->result;
}

auto EditorWorkspaceQueries::project_cache() const noexcept -> ProjectCache {
    return cached_projects;
}

EditorAnalysis::EditorAnalysis(std::shared_ptr<EditorWorkspaceQueries> queries) noexcept
    : queries(std::move(queries)) {}

auto EditorAnalysis::syntax(std::string_view document) const noexcept
    -> std::optional<EditorSyntaxQuery> {
    const auto entry = queries->document(document);
    if (!entry) {
        return std::nullopt;
    }
    return EditorSyntaxQuery {.version = entry->version, .result = entry->queries->syntax()};
}

auto EditorAnalysis::document_symbols(std::string_view document) const noexcept
    -> std::optional<EditorSymbolsQuery> {
    const auto entry = queries->document(document);
    if (!entry) {
        return std::nullopt;
    }
    return EditorSymbolsQuery {
        .document =
            EditorSyntaxQuery {.version = entry->version, .result = entry->queries->syntax()},
        .result = entry->queries->symbols(),
    };
}

auto EditorAnalysis::workspace_symbols() const noexcept
    -> std::shared_ptr<const EditorWorkspaceSymbolList> {
    return queries->symbols();
}

auto EditorAnalysis::semantic(std::span<const EditorProjectModule> project) const noexcept
    -> EditorSemanticQuery {
    auto resolved = queries->semantic(project);
    return EditorSemanticQuery {
        .result = resolved.node->result(),
        .documents = std::move(resolved.versions)
    };
}

auto EditorAnalysis::hover(
    std::span<const EditorProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> EditorHoverQuery {
    auto analysis = semantic(project);
    const auto result = analysis.result->hover(document, offset);
    return EditorHoverQuery {.analysis = std::move(analysis), .result = result};
}

auto EditorAnalysis::definition(
    std::span<const EditorProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> EditorDefinitionQuery {
    auto analysis = semantic(project);
    const auto target = analysis.result->definition(document, offset);
    auto result = std::optional<EditorVersionedLocation>();
    if (target) {
        if (const auto entry = queries->document(target->document)) {
            result.emplace(
                EditorVersionedLocation {
                    .document = target->document,
                    .version = entry->version,
                    .range = target->range
                }
            );
        }
    }
    return EditorDefinitionQuery {.analysis = std::move(analysis), .result = std::move(result)};
}

auto EditorAnalysis::references(
    std::span<const EditorProjectModule> project,
    std::string_view document,
    std::uint32_t offset
) const noexcept -> EditorReferencesQuery {
    auto analysis = semantic(project);
    const auto locations = analysis.result->references(document, offset);
    auto result = std::optional<std::vector<EditorVersionedLocation>>();
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
    return EditorReferencesQuery {.analysis = std::move(analysis), .result = std::move(result)};
}

auto EditorAnalysis::counts() const noexcept -> EditorQueryCounts {
    return *queries->counts();
}

EditorAnalysisHost::EditorAnalysisHost() noexcept
    : queries(
          std::make_shared<EditorWorkspaceQueries>(
              DocumentMap(),
              std::make_shared<EditorQueryCounts>(EditorQueryCounts {
                  .syntax = 0uz,
                  .document_symbols = 0uz,
                  .workspace_symbols = 0uz,
                  .semantic = 0uz
              }),
              nullptr,
              ProjectCache()
          )
      ) {}

auto EditorAnalysisHost::update(
    std::string document,
    std::int64_t version,
    std::string text
) noexcept -> std::expected<EditorDocumentChange, EditorDocumentUpdateFailure> {
    const auto previous = queries->document(document);
    if (previous && version <= previous->version) {
        return std::unexpected(EditorDocumentUpdateFailure(
            EditorStaleDocumentVersion {
                .current = previous->version,
                .received = version,
            }
        ));
    }
    auto inputs = queries->documents();
    auto change = EditorDocumentChange::VersionOnly;
    if (previous && previous->queries->source().text == text) {
        inputs.insert_or_assign(
            document,
            DocumentEntry {.version = version, .queries = previous->queries}
        );
    } else {
        auto source = EditorDocumentSource::create(document, std::move(text));
        if (!source) {
            return std::unexpected(EditorDocumentUpdateFailure(std::move(source.error())));
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
        change = previous ? EditorDocumentChange::Changed : EditorDocumentChange::Added;
    }
    queries = std::make_shared<EditorWorkspaceQueries>(
        std::move(inputs),
        queries->counts(),
        queries->index_baseline(),
        queries->project_cache()
    );
    return change;
}

auto EditorAnalysisHost::remove(std::string_view document) noexcept -> bool {
    if (!queries->document(document)) {
        return false;
    }
    auto inputs = queries->documents();
    inputs.erase(inputs.find(document));
    queries = std::make_shared<EditorWorkspaceQueries>(
        std::move(inputs),
        queries->counts(),
        queries->index_baseline(),
        queries->project_cache()
    );
    return true;
}

auto EditorAnalysisHost::snapshot() const noexcept -> EditorAnalysis {
    return EditorAnalysis(queries);
}
