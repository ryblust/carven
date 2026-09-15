module carven:graver.source;

import :diagnostics.diagnostic;
import :frontend.lex.token;
import :source.text;
import std;

namespace graver {

enum class TriviaKind {
    HorizontalWhitespace,
    LineEnding,
    LineComment,
};

struct Trivia final {
    TriviaKind kind;
    Span span;
};

// Owns the text and lexical data. Views expire when this owner moves or dies.
// Source identity remains in the caller's SourceManager domain for diagnostics
// and parsing; that manager must retain the matching, unchanged source.
class Source final {
public:
    Source(const Source&) = delete;
    Source(Source&&) = default;
    auto operator=(const Source&) -> Source& = delete;
    auto operator=(Source&&) -> Source& = delete;

    static auto scan(SourceView source) noexcept -> std::expected<Source, Diagnostics>;

    auto text() const noexcept -> std::string_view;
    auto token_buffer() const noexcept -> const TokenBuffer&;
    // Index N denotes the final gap after N tokens, including a tokenless file.
    auto trivia_before(std::size_t token_index) const noexcept -> std::span<const Trivia>;
    auto spelling(Span span) const noexcept -> std::string_view;

private:
    struct TriviaRange final {
        std::size_t start;
        std::size_t count;
    };

    Source(std::string text, TokenBuffer tokens) noexcept;
    auto append_gap(Span span) noexcept -> void;

    std::string source_text;
    TokenBuffer lexical_tokens;
    std::vector<Trivia> trivia;
    std::vector<TriviaRange> gaps;
};

}
