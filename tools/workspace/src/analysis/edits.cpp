module carven:workspace.analysis.edits.impl;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :format;
import :source.identifier;
import :source.text;
import :workspace.analysis;
import :workspace.semantic;
import std;

auto WorkspaceAnalysisSnapshot::formatting(std::string_view document) const noexcept
    -> std::optional<WorkspaceFormattingQuery> {
    auto parsed = syntax(document);
    if (!parsed) {
        return std::nullopt;
    }
    auto formatted = format_document(*parsed->result);
    if (!formatted) {
        return WorkspaceFormattingQuery {*parsed, std::unexpected(std::move(formatted.error()))};
    }
    const auto before = parsed->result->source().text;
    const auto& after = *formatted;
    auto prefix = 0uz;
    while (prefix < before.size() && prefix < after.size() && before[prefix] == after[prefix]) {
        ++prefix;
    }
    if (prefix == before.size() && prefix == after.size()) {
        return WorkspaceFormattingQuery {*parsed, std::optional<WorkspaceTextEdit>()};
    }
    // Keep both ends on UTF-8 boundaries when a code point shares some bytes.
    while (prefix > 0
           && prefix < before.size()
           && (static_cast<unsigned char>(before[prefix]) & 0xc0) == 0x80) {
        --prefix;
    }
    if (prefix > 0
        && prefix < before.size()
        && before[prefix] == '\n'
        && before[prefix - 1] == '\r') {
        --prefix;
    }
    auto old_end = before.size();
    auto new_end = after.size();
    while (old_end > prefix && new_end > prefix && before[old_end - 1] == after[new_end - 1]) {
        --old_end;
        --new_end;
    }
    while (old_end < before.size()
           && (static_cast<unsigned char>(before[old_end]) & 0xc0) == 0x80) {
        ++old_end;
        ++new_end;
    }
    // A position between CR and LF is indistinguishable from the next line.
    if (old_end > 0
        && old_end < before.size()
        && before[old_end] == '\n'
        && before[old_end - 1] == '\r') {
        ++old_end;
        ++new_end;
    }
    return WorkspaceFormattingQuery {
        *parsed,
        WorkspaceTextEdit {
            std::string(document),
            Span::from_bounds(
                static_cast<std::uint32_t>(prefix),
                static_cast<std::uint32_t>(old_end)
            ),
            after.substr(prefix, new_end - prefix)
        }
    };
}

auto WorkspaceAnalysisSnapshot::code_actions(
    std::span<const WorkspaceProjectModule> project,
    std::string_view document,
    Span visible,
    bool quick_fixes,
    bool organize
) const noexcept -> WorkspaceCodeActionsQuery {
    auto analysis = semantic(project);
    auto actions = std::vector<WorkspaceCodeAction>();
    const auto& sources = analysis.result->sources();
    for (const auto& diagnostic : analysis.result->diagnostics()) {
        if (!quick_fixes || !diagnostic.attachment.primary) {
            continue;
        }
        const auto location = diagnostic.attachment.primary->span;
        const auto source = sources.try_view(location.source_id);
        if (!source
            || source->origin != document
            || location.span.end() < visible.start()
            || visible.end() < location.span.start()) {
            continue;
        }
        if (diagnostic.finding.code == DiagnosticCode::NameUnresolved
            || diagnostic.finding.code == DiagnosticCode::TypeUnresolved) {
            const auto name = slice(source->text, location.span);
            if (std::holds_alternative<OrdinaryIdentifier>(classify_identifier(name))) {
                for (const auto& imported : auto_imports(
                         project,
                         document,
                         diagnostic.finding.code == DiagnosticCode::TypeUnresolved
                     )) {
                    if (imported.candidate.name == name) {
                        actions.push_back(
                            {std::format("Import '{}' from '{}'", name, imported.module_path),
                             diagnostic.finding,
                             location.span,
                             {imported.edit},
                             WorkspaceCodeActionKind::QuickFix}
                        );
                    }
                }
            }
        }
        for (const auto& fix : diagnostic.attachment.fixes) {
            auto action = WorkspaceCodeAction {
                fix.title,
                diagnostic.finding,
                location.span,
                {},
                WorkspaceCodeActionKind::QuickFix
            };
            for (const auto& edit : fix.edits) {
                const auto target = sources.try_view(edit.span.source_id);
                if (!target || std::ranges::none_of(project, [&](const auto& project_module) {
                        return project_module.document == target->origin;
                    })) {
                    action.edits.clear();
                    break;
                }
                action.edits.push_back(
                    {std::string(target->origin), edit.span.span, edit.replacement}
                );
            }
            if (!action.edits.empty()) {
                actions.push_back(std::move(action));
            }
        }
    }
    if (organize) {
        auto edits = organize_imports(project, document, *analysis.result);
        if (!edits.empty()) {
            actions.push_back(
                {"Organize imports",
                 std::nullopt,
                 Span::at(0),
                 std::move(edits),
                 WorkspaceCodeActionKind::OrganizeImports}
            );
        }
    }
    return {std::move(analysis), std::move(actions)};
}
