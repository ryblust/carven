module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.lex.interpolation;

import :test.internal.frontend.lex.fixture;
import std;

TEST_CASE("Lexer: interpolation prefix and scoped delimiters") {
    const auto expected = std::to_array<TokenCase>({
        {"f", TokenKind::Identifier},
        {"\"x\"", TokenKind::StringLiteral},
        {"f\"", TokenKind::InterpolationStart},
        {"a{{b}}", TokenKind::InterpolationText},
        {"{", TokenKind::InterpolationOpen},
        {"x", TokenKind::Identifier},
        {"::", TokenKind::ColonColon},
        {"y", TokenKind::Identifier},
        {":", TokenKind::InterpolationSpec},
        {"0", TokenKind::InterpolationText},
        {"{", TokenKind::InterpolationOpen},
        {"w", TokenKind::Identifier},
        {"}", TokenKind::InterpolationClose},
        {"x", TokenKind::InterpolationText},
        {"}", TokenKind::InterpolationClose},
        {"\"", TokenKind::InterpolationEnd},
    });
    check_token_sequence(R"(f "x" f"a{{b}}{x::y:0{w}x}")", expected);
}

TEST_CASE("Lexer: interpolation escapes stay text and retain source spans") {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = R"(f"\u{7b}\0\u{7d}{{}}")",
        .origin = "interpolation.cv"
    };
    const auto result = lex(source);
    REQUIRE(result.diagnostics.empty());
    REQUIRE(result.value.tokens().size() == 3uz);
    const auto* text = std::get_if<InterpolationTextValue>(&result.value.literal_value(1));
    REQUIRE(text != nullptr);
    CHECK(text->bytes == std::string_view("{\0}{}", 5));
    CHECK(slice(source.text, result.value.tokens()[1].span) == R"(\u{7b}\0\u{7d}{{}})");
    const auto invalid =
        std::array {R"(f"}")", R"(f"{(x]}")", R"(f"abc)", R"(f"{x)", "f\"a\nb\"", R"(f"\u{d800}")"};
    for (const auto* input : invalid) {
        const auto failure =
            lex(SourceView {.source_id = source.source_id, .text = input, .origin = source.origin});
        CAPTURE(input);
        CHECK(!failure.diagnostics.empty());
    }
}

TEST_CASE("Lexer: nested interpolation has a bounded scanning depth") {
    auto source = std::string();
    for (auto index = 0uz; index < 513uz; ++index) {
        source += "f\"{";
    }
    source += '0';
    for (auto index = 0uz; index < 513uz; ++index) {
        source += "}\"";
    }
    const auto result =
        lex(SourceView {
            .source_id = SourceID::from_index(0),
            .text = source,
            .origin = "interpolation-depth.cv"
        });
    REQUIRE(!result.diagnostics.empty());
    CHECK(result.diagnostics.front().finding.message == "interpolation nesting limit exceeded");
}
