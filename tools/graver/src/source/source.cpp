module carven:graver.source.impl;

import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :frontend.lex.token;
import :frontend.lex;
import :graver.source;
import :source.text;
import :support.invariant;
import std;

FormattingSource::FormattingSource(std::string text, TokenBuffer tokens) noexcept
    : source_text(std::move(text)),
      lexical_tokens(std::move(tokens)) {
    const auto source_size = static_cast<std::uint32_t>(source_text.size());
    gaps.reserve(lexical_tokens.tokens().size() + 1uz);
    auto position = 0u;
    for (const auto token : lexical_tokens.tokens()) {
        if (token.span.empty() || token.span.start() < position || token.span.end() > source_size) {
            invariant_violation("Graver token spans must be nonempty, ordered and within source");
        }
        append_gap(Span::from_bounds(position, token.span.start()));
        position = token.span.end();
    }
    append_gap(Span::from_bounds(position, source_size));
}

auto FormattingSource::scan(SourceView source) noexcept
    -> std::expected<FormattingSource, Diagnostics> {
    auto scanned = lex(source);
    if (has_errors(scanned)) {
        return std::unexpected(std::move(scanned.diagnostics));
    }
    return FormattingSource(std::string(source.text), std::move(scanned.value));
}

auto FormattingSource::append_gap(Span span) noexcept -> void {
    const auto first = trivia.size();
    auto position = span.start();
    while (position < span.end()) {
        const auto start = position;
        const auto value = source_text[position];
        auto kind = SourceTriviaKind::HorizontalWhitespace;
        if (value == ' ' || value == '\t') {
            do {
                ++position;
            } while (position < span.end()
                     && (source_text[position] == ' ' || source_text[position] == '\t'));
        } else if (value == '\n' || value == '\r') {
            kind = SourceTriviaKind::LineEnding;
            ++position;
            if (value == '\r' && position < span.end() && source_text[position] == '\n') {
                ++position;
            }
        } else if (value == '/'
                   && position + 1u < span.end()
                   && source_text[position + 1u] == '/') {
            kind = SourceTriviaKind::LineComment;
            position += 2u;
            while (position < span.end()
                   && source_text[position] != '\n'
                   && source_text[position] != '\r') {
                ++position;
            }
        } else {
            invariant_violation("Graver found non-trivia in a successful lexer's source gap");
        }
        trivia.push_back(SourceTrivia {.kind = kind, .span = Span::from_bounds(start, position)});
    }
    gaps.push_back(TriviaRange {.start = first, .count = trivia.size() - first});
}

auto FormattingSource::text() const noexcept -> std::string_view {
    return source_text;
}

auto FormattingSource::token_buffer() const noexcept -> const TokenBuffer& {
    return lexical_tokens;
}

auto FormattingSource::trivia_before(std::size_t token_index) const noexcept
    -> std::span<const SourceTrivia> {
    if (token_index >= gaps.size()) {
        invariant_violation("Graver trivia lookup used an invalid token boundary");
    }
    const auto range = gaps[token_index];
    return std::span<const SourceTrivia>(trivia).subspan(range.start, range.count);
}

auto FormattingSource::spelling(Span span) const noexcept -> std::string_view {
    return slice(source_text, span);
}
