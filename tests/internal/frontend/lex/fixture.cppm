module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.lex.fixture;

import :frontend.lex;
import :frontend.lex.token;
import :source.text;
import std;

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
    CAPTURE(spelling);
    REQUIRE(result.diagnostics.empty());
    const auto tokens = result.value.tokens();
    REQUIRE_EQ(tokens.size(), 1u);
    CHECK_EQ(tokens[0].kind, kind);
    CHECK_EQ(tokens[0].span.start(), 0u);
    CHECK_EQ(tokens[0].span.end(), spelling.size());
}

auto check_tokens(std::span<const TokenCase> cases) noexcept -> void {
    for (const auto& test : cases) {
        check_token(test.spelling, test.kind);
    }
}

auto check_token_sequence(std::string_view text, std::span<const TokenCase> expected) noexcept
    -> void {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    CAPTURE(text);
    REQUIRE(result.diagnostics.empty());
    const auto tokens = result.value.tokens();
    REQUIRE_EQ(tokens.size(), expected.size());
    for (const auto& [token, expected_token] : std::views::zip(tokens, expected)) {
        CHECK_EQ(token.kind, expected_token.kind);
        CHECK_EQ(slice(text, token.span), expected_token.spelling);
    }
}

auto check_lexical_error(std::string_view text) noexcept -> void {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    CAPTURE(text);
    CHECK(!result.diagnostics.empty());
    CHECK(std::ranges::any_of(result.value.tokens(), [](const Token& token) static noexcept {
        return token.kind == TokenKind::Invalid;
    }));
}

auto check_not_single_number(std::string_view text) noexcept -> void {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    CAPTURE(text);
    const auto tokens = result.value.tokens();
    CHECK(
        !(result.diagnostics.empty()
          && tokens.size() == 1
          && tokens[0].kind == TokenKind::NumberLiteral)
    );
}
