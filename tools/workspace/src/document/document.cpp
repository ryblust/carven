module carven:workspace.document.impl;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.lex;
import :frontend.parse;
import :source.manager;
import :source.text;
import :workspace.document;
import std;

WorkspaceDocumentSource::WorkspaceDocumentSource(SourceManager sources, SourceID id) noexcept
    : source_manager(std::move(sources)),
      source_id(id) {}

auto WorkspaceDocumentSource::create(std::string document, std::string text) noexcept
    -> std::expected<std::shared_ptr<const WorkspaceDocumentSource>, SourceLoadError> {
    auto sources = SourceManager();
    const auto id = sources.append_virtual(std::move(document), std::move(text));
    if (!id) {
        return std::unexpected(id.error());
    }
    return std::shared_ptr<const WorkspaceDocumentSource>(
        new WorkspaceDocumentSource(std::move(sources), *id)
    );
}

auto WorkspaceDocumentSource::source() const noexcept -> SourceView {
    return source_manager.view(source_id);
}

auto WorkspaceDocumentSource::sources() const noexcept -> const SourceManager& {
    return source_manager;
}

WorkspaceDocumentSyntax::WorkspaceDocumentSyntax(
    std::shared_ptr<const WorkspaceDocumentSource> source,
    std::optional<SyntaxTree> syntax,
    Diagnostics diagnostics
) noexcept
    : input(std::move(source)),
      tree(std::move(syntax)),
      findings(std::move(diagnostics)) {}

auto WorkspaceDocumentSyntax::source() const noexcept -> SourceView {
    return input->source();
}

auto WorkspaceDocumentSyntax::sources() const noexcept -> const SourceManager& {
    return input->sources();
}

auto WorkspaceDocumentSyntax::syntax() const noexcept -> std::optional<ASTView> {
    if (std::ranges::any_of(findings, [](const Diagnostic& diagnostic) static noexcept {
            return diagnostic.finding.severity == DiagnosticSeverity::Error;
        })) {
        return std::nullopt;
    }
    return recovered_syntax();
}

auto WorkspaceDocumentSyntax::recovered_syntax() const noexcept -> std::optional<ASTView> {
    return tree ? std::optional(tree->view()) : std::nullopt;
}

auto WorkspaceDocumentSyntax::diagnostics() const noexcept -> std::span<const Diagnostic> {
    return findings;
}

auto parse_workspace_document(std::shared_ptr<const WorkspaceDocumentSource> source) noexcept
    -> std::shared_ptr<const WorkspaceDocumentSyntax> {
    auto lexical = lex(source->source());
    auto syntax = std::optional<SyntaxTree>();
    if (!has_errors(lexical)) {
        auto parsed = parse_recovering(source->sources(), lexical.value);
        syntax = std::move(parsed.value);
        lexical.diagnostics.append_range(std::move(parsed.diagnostics));
    }
    return std::make_shared<const WorkspaceDocumentSyntax>(
        std::move(source),
        std::move(syntax),
        std::move(lexical.diagnostics)
    );
}
