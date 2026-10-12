module carven:frontend.document;

import :diagnostics.diagnostic;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :source.document;
import :source.manager;
import :source.text;
import :support.timing;
import std;

class ParsedDocument final {
public:
    auto source() const noexcept -> SourceView;
    auto sources() const noexcept -> const SourceManager&;
    // Complete syntax remains unavailable whenever parsing reports errors.
    auto syntax() const noexcept -> std::optional<ASTView>;
    // Includes complete retained declarations around recovered top-level errors.
    // Lexical, delimiter, and initial import failures still return no tree.
    auto recovered_syntax() const noexcept -> std::optional<ASTView>;
    // Shares immutable AST storage, binding its source coordinates to one compilation.
    auto bind(SourceID source) const noexcept -> std::optional<SyntaxTree>;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;

private:
    ParsedDocument(
        std::shared_ptr<const SourceDocument> source,
        std::optional<SyntaxTree> syntax,
        Diagnostics diagnostics
    ) noexcept;

    friend auto parse_document(
        std::shared_ptr<const SourceDocument> source,
        TimingOutput timings
    ) noexcept -> std::shared_ptr<const ParsedDocument>;

    // The source owner remains alive for all AST and diagnostic source identities.
    std::shared_ptr<const SourceDocument> input;
    std::optional<SyntaxTree> tree;
    Diagnostics findings;
};

auto parse_document(
    std::shared_ptr<const SourceDocument> source,
    TimingOutput timings = {}
) noexcept -> std::shared_ptr<const ParsedDocument>;
