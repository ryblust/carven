module carven:workspace.analysis.imports.impl;

import :diagnostics.code;
import :format;
import :frontend.ast.decl;
import :frontend.ast.storage;
import :frontend.document;
import :frontend.lex.token;
import :frontend.program.parse;
import :semantic.analysis.source;
import :semantic.analysis.source.imports;
import :source.manager;
import :source.standard;
import :source.text;
import :workspace.analysis;
import :workspace.semantic;
import std;

namespace {

using Selection = std::pair<std::string, std::string>;

auto selection(std::string_view text, const ASTImportItem& item) noexcept -> Selection {
    return {
        std::string(slice(text, item.name_span)),
        item.alias_span ? std::string(slice(text, *item.alias_span)) : std::string()
    };
}

auto selections(std::string_view text, const ASTModuleImport& imported) noexcept
    -> std::vector<Selection> {
    auto result = std::vector<Selection>();
    if (const auto* single = std::get_if<ASTSingleImport>(&imported.selection.value)) {
        result.push_back(selection(text, single->item));
    }
    if (const auto* list = std::get_if<ASTImportList>(&imported.selection.value)) {
        for (const auto& item : list->names) {
            result.push_back(selection(text, item));
        }
    }
    return result;
}

auto render_selection(std::vector<Selection> names) noexcept -> std::string {
    std::ranges::sort(names);
    names.erase(std::unique(names.begin(), names.end()), names.end());
    auto result = std::string();
    for (const auto& [name, alias] : names) {
        if (!result.empty()) {
            result += ", ";
        }
        result += name;
        if (!alias.empty()) {
            result += " as " + alias;
        }
    }
    return names.size() == 1 ? result + ";" : "{ " + result + " }";
}

auto complete_import(const ParsedDocument& document, const ASTModuleImport& imported) noexcept
    -> bool {
    const auto text = document.source().text;
    return !imported.span.empty()
        && imported.span.end() <= text.size()
        && text[imported.span.end() - 1]
        == (std::holds_alternative<ASTImportList>(imported.selection.value) ? '}' : ';')
        && std::ranges::none_of(document.diagnostics(), [&](const auto& diagnostic) noexcept {
               return diagnostic.attachment.primary
                   && diagnostic.attachment.primary->span.span.start() >= imported.span.start()
                   && diagnostic.attachment.primary->span.span.start() <= imported.span.end();
           });
}

auto line_ending(std::string_view text) noexcept -> std::string_view {
    return text.contains("\r\n") ? "\r\n" : "\n";
}

auto add_import(
    const ParsedDocument& document,
    std::string_view key,
    const CanonicalModulePath& importer,
    const CanonicalModulePath& target,
    std::string_view name
) noexcept -> std::optional<WorkspaceTextEdit> {
    const auto text = document.source().text;
    const auto syntax = document.recovered_syntax();
    if (syntax) {
        for (const auto& imported : syntax->module_imports()) {
            const auto path = resolve_import_path(text, importer, imported.module_reference);
            if (!path || *path != target) {
                continue;
            }
            if (std::holds_alternative<ASTWildcardImport>(imported.selection.value)) {
                return std::nullopt;
            }
            auto names = selections(text, imported);
            if (std::ranges::any_of(names, [&](const auto& value) noexcept {
                    return value.first == name && value.second.empty();
                })) {
                return std::nullopt;
            }
            if (!complete_import(document, imported) || slice(text, imported.span).contains("//")) {
                continue;
            }
            names.emplace_back(name, "");
            return WorkspaceTextEdit {
                .document = std::string(key),
                .range = Span::from_bounds(imported.selection.span.start(), imported.span.end()),
                .replacement = render_selection(std::move(names))
            };
        }
    }
    const auto reference = source_import_reference(importer, target);
    if (reference.empty()) {
        return std::nullopt;
    }
    // The first lexical token follows header comments even if later syntax fails.
    // Put new imports before unfinished imports rather than inside their recovery span.
    const auto tokens = document.tokens();
    const auto position =
        tokens.empty() ? static_cast<std::uint32_t>(text.size()) : tokens.front().span.start();
    auto insertion = std::format("import {} using {};{}", reference, name, line_ending(text));
    return WorkspaceTextEdit {
        .document = std::string(key),
        .range = Span::at(position),
        .replacement = std::move(insertion)
    };
}

} // namespace

auto WorkspaceAnalysisSnapshot::auto_imports(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    bool types_only,
    std::span<const SourceCompletionCandidate> visible
) const noexcept -> std::vector<WorkspaceAutoImport> {
    auto result = std::vector<WorkspaceAutoImport>();
    const auto owner = std::ranges::find(project, document, &WorkspaceProjectModule::document);
    const auto parsed = syntax(document);
    if (owner == project.end() || !parsed) {
        return result;
    }
    for (const auto& project_module : project) {
        const auto target = syntax(project_module.document);
        if (!target || target->result->source().text.empty()) {
            continue;
        }
        for (auto candidate : source_import_candidates(
                 *target->result,
                 project_module.module_path,
                 owner->module_path
             )) {
            if (types_only
                && candidate.kind != SourceCompletionKind::Structure
                && candidate.kind != SourceCompletionKind::Enumeration) {
                continue;
            }
            if (std::ranges::any_of(visible, [&](const auto& value) {
                    return value.name == candidate.name;
                })) {
                continue;
            }
            auto edit = add_import(
                *parsed->result,
                document,
                owner->module_path,
                project_module.module_path,
                candidate.name
            );
            if (edit) {
                result.push_back(
                    {std::move(candidate),
                     std::string(project_module.module_path.value()),
                     std::move(*edit)}
                );
            }
        }
    }
    // Provided modules such as std::async have no source document to enumerate.
    for (const auto& entry : provided_exports()) {
        if (entry.default_available || entry.module_local) {
            continue;
        }
        const auto kind = entry.identity == ProvidedExport::Operation
            ? SourceCompletionKind::Structure
            : SourceCompletionKind::Function;
        if (types_only && kind != SourceCompletionKind::Structure) {
            continue;
        }
        if (std::ranges::any_of(visible, [&](const auto& value) {
                return value.name == entry.name;
            })) {
            continue;
        }
        const auto module_path =
            CanonicalModulePath::from_value(provided_module_path(entry.provider));
        if (std::ranges::any_of(result, [&](const auto& value) {
                return value.module_path == module_path->value()
                    && value.candidate.name == entry.name;
            })) {
            continue;
        }
        auto edit =
            add_import(*parsed->result, document, owner->module_path, *module_path, entry.name);
        if (edit) {
            result.push_back(
                {{std::string(entry.name), kind, std::string(module_path->value())},
                 std::string(module_path->value()),
                 std::move(*edit)}
            );
        }
    }
    return result;
}

auto WorkspaceAnalysisSnapshot::organize_imports(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    const WorkspaceSemanticAnalysis& analysis
) const noexcept -> std::vector<WorkspaceTextEdit> {
    auto edits = std::vector<WorkspaceTextEdit>();
    const auto owner = std::ranges::find(project, document, &WorkspaceProjectModule::document);
    const auto parsed = syntax(document);
    if (owner == project.end() || !parsed) {
        return edits;
    }
    const auto tree = parsed->result->recovered_syntax();
    if (!tree) {
        return edits;
    }
    const auto text = parsed->result->source().text;
    auto unused = std::vector<Span>();
    // Failed construction can omit uses. Only complete checking proves absence.
    if (analysis.program()) {
        for (const auto& diagnostic : analysis.diagnostics()) {
            if (diagnostic.finding.code != DiagnosticCode::LintUnusedImport
                || !diagnostic.attachment.primary) {
                continue;
            }
            const auto location = diagnostic.attachment.primary->span;
            const auto source = analysis.sources().try_view(location.source_id);
            if (source && source->origin == document) {
                unused.push_back(location.span);
            }
        }
    }
    const auto removable = [&](Span span) {
        return std::ranges::any_of(unused, [&](Span value) {
            return value.start() <= span.start() && span.end() <= value.end();
        });
    };

    struct Entry final {
        const ASTModuleImport* imported;
        CanonicalModulePath path;
    };

    auto run = std::vector<Entry>();
    const auto flush = [&]() {
        if (run.empty()) {
            return;
        }
        struct Group final {
            std::string reference;
            std::vector<Selection> names;
        };
        auto groups = std::map<std::string, Group>();
        for (const auto& entry : run) {
            const auto& imported = *entry.imported;
            if (removable(imported.span)) {
                continue;
            }
            auto names = std::vector<Selection>();
            const auto add = [&](const ASTImportItem& item) {
                if (!removable(item.alias_span.value_or(item.name_span))) {
                    names.push_back(selection(text, item));
                }
            };
            if (const auto* single = std::get_if<ASTSingleImport>(&imported.selection.value)) {
                add(single->item);
            }
            if (const auto* list = std::get_if<ASTImportList>(&imported.selection.value)) {
                for (const auto& item : list->names) {
                    add(item);
                }
            }
            if (names.empty()) {
                continue;
            }
            auto [group, inserted] = groups.try_emplace(
                std::string(entry.path.value()),
                Group {std::string(slice(text, imported.module_reference.span)), {}}
            );
            group->second.names.append_range(names);
        }
        auto replacement = std::string();
        for (const auto& [path, group] : groups) {
            replacement +=
                std::format("import {} using {}\n", group.reference, render_selection(group.names));
        }
        if (!replacement.empty()) {
            auto sources = SourceManager();
            const auto id = sources.append_virtual("organized imports", replacement);
            const auto formatted = id
                ? format_source(sources, *id)
                : std::expected<std::string, Diagnostics>(std::unexpected(Diagnostics()));
            if (!formatted) {
                run.clear();
                return;
            }
            replacement.clear();
            for (const auto value : *formatted) {
                if (value == '\n') {
                    replacement += line_ending(text);
                } else {
                    replacement += value;
                }
            }
            // The original suffix already contains its line ending.
            replacement.resize(replacement.size() - line_ending(text).size());
        }
        const auto span =
            Span::from_bounds(run.front().imported->span.start(), run.back().imported->span.end());
        if (slice(text, span) != replacement) {
            edits.push_back({std::string(document), span, std::move(replacement)});
        }
        run.clear();
    };
    for (const auto& imported : tree->module_imports()) {
        const auto path = resolve_import_path(text, owner->module_path, imported.module_reference);
        const auto line_end = text.find_first_of("\r\n", imported.span.end());
        const auto suffix = text.substr(
            imported.span.end(),
            (line_end == std::string_view::npos ? text.size() : line_end) - imported.span.end()
        );
        if (!path
            || !complete_import(*parsed->result, imported)
            || slice(text, imported.span).contains("//")
            || suffix.contains("//")
            || std::holds_alternative<ASTWildcardImport>(imported.selection.value)) {
            flush();
            if (path
                && complete_import(*parsed->result, imported)
                && removable(imported.span)
                && !slice(text, imported.span).contains("//")
                && !suffix.contains("//")) {
                edits.push_back({std::string(document), imported.span, ""});
            }
            continue;
        }
        if (!run.empty()) {
            const auto gap = text.substr(
                run.back().imported->span.end(),
                imported.span.start() - run.back().imported->span.end()
            );
            if (gap.find_first_not_of(" \t\r\n") != std::string_view::npos
                || std::ranges::count(gap, '\n') > 1) {
                flush();
            }
        }
        run.push_back({&imported, *path});
    }
    flush();
    return edits;
}
