module carven:test.internal.frontend.lex.fixture;

import :frontend.lex;
import :frontend.lex.token;
import :source.text;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

} // namespace

struct TokenCase final {
    std::string_view spelling;
    TokenKind kind;
};

auto check_token(std::string_view spelling, TokenKind kind) noexcept -> void {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = spelling,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    if (!(ct::expect(result.diagnostics.empty()).note("spelling = ", spelling))) {
        return;
    }
    const auto tokens = result.value.tokens();
    if (!(ct::expect_equal(tokens.size(), 1uz).note("spelling = ", spelling))) {
        return;
    }
    ct::expect_equal(tokens[0].kind, kind).note("spelling = ", spelling);
    ct::expect_equal(tokens[0].span.start(), 0u).note("spelling = ", spelling);
    ct::expect_equal(tokens[0].span.end(), spelling.size()).note("spelling = ", spelling);
}

auto check_tokens(std::span<const TokenCase> cases) noexcept -> void {
    ct::each(
        cases,
        [](const TokenCase& test) static noexcept -> std::string_view { return test.spelling; },
        [](const TokenCase& test) static noexcept { check_token(test.spelling, test.kind); }
    );
}

auto check_token_sequence(std::string_view text, std::span<const TokenCase> expected) noexcept
    -> void {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    if (!(ct::expect(result.diagnostics.empty()).note("text = ", text))) {
        return;
    }
    const auto tokens = result.value.tokens();
    if (!(ct::expect_equal(tokens.size(), expected.size()).note("text = ", text))) {
        return;
    }
    for (const auto& [token, expected_token] : std::views::zip(tokens, expected)) {
        ct::expect_equal(token.kind, expected_token.kind).note("text = ", text);
        ct::expect_equal(slice(text, token.span), expected_token.spelling).note("text = ", text);
    }
}

auto check_lexical_error(std::string_view text) noexcept -> void {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    ct::expect(!result.diagnostics.empty()).note("text = ", text);
    ct::expect(std::ranges::any_of(result.value.tokens(), [](const Token& token) static noexcept {
        return token.kind == TokenKind::Invalid;
    })).note("text = ", text);
}

auto check_not_single_number(std::string_view text) noexcept -> void {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    const auto tokens = result.value.tokens();
    ct::expect(!(result.diagnostics.empty()
                 && tokens.size() == 1
                 && tokens[0].kind == TokenKind::NumberLiteral))
        .note("text = ", text);
}
