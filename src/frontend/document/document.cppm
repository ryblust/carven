module carven:frontend.document;

import :diagnostics.diagnostic;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.lex.token;
import :frontend.parse;
import :source.document;
import :source.manager;
import :source.text;
import :support.timing;
import std;

class ParsedDocument final {
public:
    auto source() const noexcept -> SourceView;
    auto sources() const noexcept -> const SourceManager&;
    // Tokens retain the source document's coordinates, including invalid regions.
    auto tokens() const noexcept -> std::span<const Token>;
    // Complete syntax is unavailable for source errors.
    auto syntax() const noexcept -> std::optional<ASTView>;
    // Retains complete imports/statements/declarations around lexical and syntax errors.
    // Irreconcilable delimiter pairing and excessive nesting can return no tree.
    auto recovered_syntax() const noexcept -> std::optional<ASTView>;
    // Shares immutable AST storage, binding its source coordinates to one compilation.
    auto bind(SourceID source) const noexcept -> std::optional<SyntaxTree>;
    auto bind_recovered(SourceID source) const noexcept -> std::optional<SyntaxTree>;
    auto diagnostics() const noexcept -> std::span<const Diagnostic>;

private:
    ParsedDocument(
        std::shared_ptr<const SourceDocument> source,
        TokenBuffer tokens,
        std::optional<SyntaxTree> syntax,
        Diagnostics diagnostics
    ) noexcept;

    friend auto parse_document(
        std::shared_ptr<const SourceDocument> source,
        TimingOutput timings
    ) noexcept -> std::shared_ptr<const ParsedDocument>;

    // The source owner remains alive for all AST and diagnostic source identities.
    std::shared_ptr<const SourceDocument> input;
    TokenBuffer lexical;
    std::optional<SyntaxTree> tree;
    Diagnostics findings;
};

auto parse_document(
    std::shared_ptr<const SourceDocument> source,
    TimingOutput timings = {}
) noexcept -> std::shared_ptr<const ParsedDocument>;
