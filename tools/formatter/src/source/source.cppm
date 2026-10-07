module carven:formatter.source;

import :diagnostics.diagnostic;
import :frontend.lex.token;
import :source.text;
import std;

enum class SourceTriviaKind {
    HorizontalWhitespace,
    LineEnding,
    LineComment,
};

struct SourceTrivia final {
    SourceTriviaKind kind;
    Span span;
};

// Owns the text and lexical data. Views expire when this owner moves or dies.
// Token source IDs remain in the caller's SourceManager domain. Passing the
// token buffer to parse requires that manager to retain matching source text.
class FormattingSource final {
public:
    FormattingSource(const FormattingSource&) = delete;
    FormattingSource(FormattingSource&&) = default;
    auto operator=(const FormattingSource&) -> FormattingSource& = delete;
    auto operator=(FormattingSource&&) -> FormattingSource& = delete;

    static auto scan(SourceView source) noexcept -> std::expected<FormattingSource, Diagnostics>;

    auto text() const noexcept -> std::string_view;
    auto token_buffer() const noexcept -> const TokenBuffer&;
    // Index N denotes the final gap after N tokens, including a tokenless file.
    auto trivia_before(std::size_t token_index) const noexcept -> std::span<const SourceTrivia>;
    auto spelling(Span span) const noexcept -> std::string_view;

private:
    struct TriviaRange final {
        std::size_t start;
        std::size_t count;
    };

    FormattingSource(std::string text, TokenBuffer tokens) noexcept;
    auto append_gap(Span span) noexcept -> void;

    std::string source_text;
    TokenBuffer lexical_tokens;
    std::vector<SourceTrivia> trivia;
    std::vector<TriviaRange> gaps;
};
