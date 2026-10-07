module carven:workspace.analysis.impl;

import :frontend.program.parse;
import :source.text;
import :support.timing;
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

class CachedAnalysis final {
public:
    CachedAnalysis(
        std::vector<WorkspaceSemanticInput> inputs,
        std::shared_ptr<WorkspaceQueryCounts> counts
    ) noexcept;
    auto matches_documents(const DocumentMap& documents) const noexcept -> bool;
    auto matches_modules(std::span<const WorkspaceProjectModule> modules) const noexcept -> bool;
    auto result(TimingOutput timings) noexcept -> std::shared_ptr<const WorkspaceSemanticAnalysis>;

private:
    std::vector<WorkspaceSemanticInput> inputs;
    std::shared_ptr<WorkspaceQueryCounts> work_counts;
    std::shared_ptr<const WorkspaceSemanticAnalysis> cached_result;
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
        std::shared_ptr<WorkspaceQueryCounts> counts,
        std::shared_ptr<const WorkspaceIndex> previous_index,
        std::vector<std::shared_ptr<CachedAnalysis>> cached_selections
    ) noexcept;
    auto document(std::string_view key) const noexcept -> std::optional<DocumentEntry>;
    auto documents() const noexcept -> DocumentMap;
    auto counts() const noexcept -> std::shared_ptr<WorkspaceQueryCounts>;
    auto index_baseline() const noexcept -> std::shared_ptr<const WorkspaceIndex>;
    auto symbols() noexcept -> std::shared_ptr<const WorkspaceSymbolList>;
    auto resolve(std::span<const WorkspaceProjectModule> project) noexcept -> ResolvedSelection;
    auto selections() const noexcept -> std::vector<std::shared_ptr<CachedAnalysis>>;
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
    std::shared_ptr<WorkspaceQueryCounts> work_counts;
    std::shared_ptr<const WorkspaceIndex> previous_index;
    std::shared_ptr<const WorkspaceIndex> cached_index;
    std::vector<std::shared_ptr<CachedAnalysis>> cached_selections;
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
    std::vector<std::shared_ptr<CachedAnalysis>> cached_selections
) noexcept
    : inputs(std::move(inputs)),
      work_counts(std::move(counts)),
      previous_index(std::move(previous_index)),
      cached_selections(std::move(cached_selections)) {
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

CachedAnalysis::CachedAnalysis(
    std::vector<WorkspaceSemanticInput> inputs,
    std::shared_ptr<WorkspaceQueryCounts> counts
) noexcept
    : inputs(std::move(inputs)),
      work_counts(std::move(counts)) {}

auto CachedAnalysis::matches_documents(const DocumentMap& documents) const noexcept -> bool {
    return std::ranges::all_of(inputs, [&](const WorkspaceSemanticInput& input) noexcept {
        const auto found = documents.find(input.module.document);
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
           const WorkspaceProjectModule& module) static noexcept { return input.module == module; }
    );
}

auto CachedAnalysis::result(TimingOutput timings) noexcept
    -> std::shared_ptr<const WorkspaceSemanticAnalysis> {
    if (!cached_result) {
        ++work_counts->semantic;
        cached_result = analyze_workspace_project(inputs, timings);
    }
    return cached_result;
}

auto WorkspaceQueries::resolve(std::span<const WorkspaceProjectModule> project) noexcept
    -> ResolvedSelection {
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
        for (const auto& module : modules) {
            const auto entry = document(module.document);
            resolved.push_back(
                {.module = module, .source = entry ? entry->queries->source_owner() : nullptr}
            );
        }
        node = std::make_shared<CachedAnalysis>(std::move(resolved), work_counts);
        if (cached_selections.size() == 2uz) {
            cached_selections.erase(cached_selections.begin());
        }
    }
    cached_selections.push_back(node);
    auto versions = std::map<std::string_view, std::int64_t>();
    for (const auto& module : modules) {
        if (const auto entry = document(module.document)) {
            versions.emplace(module.document, entry->version);
        }
    }
    auto document_versions = std::vector<WorkspaceDocumentVersion>();
    for (const auto& [document, version] : versions) {
        document_versions.push_back({.document = std::string(document), .version = version});
    }
    return {.node = std::move(node), .versions = std::move(document_versions)};
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
    for (const auto& module : project) {
        if (!by_path.emplace(module.module_path.value(), &module).second
            || !documents.insert(module.document).second) {
            valid_project = false;
        }
        if (module.document == document) {
            root = &module;
        }
    }
    if (root == nullptr) {
        return std::nullopt;
    }
    if (!valid_project) {
        return std::vector<WorkspaceProjectModule>(project.begin(), project.end());
    }
    auto pending = std::vector<const WorkspaceProjectModule*> {root};
    auto selected = std::set<std::string_view>();
    auto modules = std::vector<WorkspaceProjectModule>();
    for (auto index = 0uz; index < pending.size(); ++index) {
        const auto& module = *pending[index];
        if (!selected.insert(module.module_path.value()).second) {
            continue;
        }
        modules.push_back(module);
        const auto entry = this->document(module.document);
        if (!entry) {
            continue;
        }
        const auto parsed = entry->queries->syntax();
        const auto ast = parsed->recovered_syntax();
        if (!ast) {
            continue;
        }
        for (const auto& import : ast->module_imports()) {
            const auto path = resolve_import_path(
                parsed->source().text,
                module.module_path,
                import.module_reference
            );
            if (!path) {
                continue;
            }
            const auto found = by_path.find(path->value());
            if (found == by_path.end()) {
                continue;
            }
            pending.push_back(found->second);
        }
    }
    return modules;
}

auto WorkspaceQueries::navigation(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document
) noexcept -> std::optional<ResolvedSelection> {
    const auto modules = navigation_modules(project, document);
    return modules ? std::optional(resolve(*modules)) : std::nullopt;
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
    std::span<const WorkspaceProjectModule> project,
    TimingOutput timings
) const noexcept -> WorkspaceSemanticQuery {
    auto resolved = queries->resolve(project);
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
        .result = resolved->node->result({}),
        .documents = std::move(resolved->versions)
    };
    const auto result = analysis.result->hover(document, offset);
    return WorkspaceHoverQuery {.analysis = std::move(analysis), .result = result};
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
        .result = resolved->node->result({}),
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
              std::vector<std::shared_ptr<CachedAnalysis>>()
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
        queries->selections()
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
        queries->selections()
    );
    return true;
}

auto WorkspaceAnalysisHost::snapshot() const noexcept -> WorkspaceAnalysisSnapshot {
    return WorkspaceAnalysisSnapshot(queries);
}
