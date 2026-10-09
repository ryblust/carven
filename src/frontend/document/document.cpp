module carven:frontend.document.impl;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.document;
import :frontend.lex;
import :frontend.parse;
import :source.document;
import :source.manager;
import :source.text;
import :support.timing;
import std;

ParsedDocument::ParsedDocument(
    std::shared_ptr<const SourceDocument> source,
    std::optional<SyntaxTree> syntax,
    Diagnostics diagnostics
) noexcept
    : input(std::move(source)),
      tree(std::move(syntax)),
      findings(std::move(diagnostics)) {}

auto ParsedDocument::source() const noexcept -> SourceView {
    return input->source();
}

auto ParsedDocument::sources() const noexcept -> const SourceManager& {
    return input->sources();
}

auto ParsedDocument::syntax() const noexcept -> std::optional<ASTView> {
    if (std::ranges::any_of(findings, [](const Diagnostic& diagnostic) static noexcept {
            return diagnostic.finding.severity == DiagnosticSeverity::Error;
        })) {
        return std::nullopt;
    }
    return recovered_syntax();
}

auto ParsedDocument::recovered_syntax() const noexcept -> std::optional<ASTView> {
    return tree ? std::optional(tree->view()) : std::nullopt;
}

auto ParsedDocument::bind(SourceID source) const noexcept -> std::optional<SyntaxTree> {
    return syntax() ? std::optional(tree->bind(source)) : std::nullopt;
}

auto ParsedDocument::diagnostics() const noexcept -> std::span<const Diagnostic> {
    return findings;
}

auto parse_document(std::shared_ptr<const SourceDocument> source, TimingOutput timings) noexcept
    -> std::shared_ptr<const ParsedDocument> {
    auto lexical = lex(source->source(), timings);
    auto syntax = std::optional<SyntaxTree>();
    if (!has_errors(lexical)) {
        auto parsed = parse_recovering(source->sources(), lexical.value, timings);
        syntax = std::move(parsed.value);
        lexical.diagnostics.append_range(std::move(parsed.diagnostics));
    }
    return std::shared_ptr<const ParsedDocument>(
        new ParsedDocument(std::move(source), std::move(syntax), std::move(lexical.diagnostics))
    );
}
