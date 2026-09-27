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

TEST_CASE("Lexer: multiline interpolation excludes hole code from text layout") {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "f\"\"\"\n    Result: {\n0\n    :04}\n      {{done}}\\n\n\"\"\";",
        .origin = "multiline.cv",
    };
    const auto result = lex(source);
    REQUIRE(result.diagnostics.empty());
    auto text = std::vector<std::string>();
    for (auto index = 0uz; index < result.value.tokens().size(); ++index) {
        if (result.value.tokens()[index].kind == TokenKind::InterpolationText) {
            text.push_back(
                std::get<InterpolationTextValue>(result.value.literal_value(index)).bytes
            );
        }
    }
    CHECK(text == std::vector<std::string> {"Result: ", "04", "\n  {done}\n"});
    CHECK(slice(source.text, result.value.tokens().front().span) == "f\"\"\"\n");
    CHECK(result.value.tokens().back().kind == TokenKind::Semicolon);
}

TEST_CASE("Lexer: multiline interpolation shares text block boundary rules") {
    struct Case final {
        std::string_view source;
        std::string_view expected;
    };

    const auto cases = std::to_array<Case>({
        {"f\"\"\"\n\"\"\"", ""},
        {"f\"\"\"\n  \n \t\n\"\"\"", "\n"},
        {"f\"\"\"\r\n  A\r\n    B\r\n\"\"\"", "A\n  B"},
        {"f\"\"\"\n  \\n\\u{20}\\0\n\"\"\"", std::string_view("\n \0", 3)},
        {"f\"\"\"\n  \"quoted\"\n  {{literal}}\n\"\"\"", "\"quoted\"\n{literal}"},
        {"f\"\"\"\n    {\n0\n}\n      tail\n\"\"\"", "\n  tail"},
        {"f\"\"\"\n  before {\n0\n} after\n    tail\n\"\"\"", "before  after\n  tail"},
    });
    for (const auto& item : cases) {
        CAPTURE(item.source);
        const auto result =
            lex(SourceView {
                .source_id = SourceID::from_index(0),
                .text = item.source,
                .origin = "multiline.cv",
            });
        REQUIRE(result.diagnostics.empty());
        auto text = std::string();
        for (auto index = 0uz; index < result.value.tokens().size(); ++index) {
            if (result.value.tokens()[index].kind == TokenKind::InterpolationText) {
                const auto* value =
                    std::get_if<InterpolationTextValue>(&result.value.literal_value(index));
                REQUIRE(value != nullptr);
                text += value->bytes;
            }
        }
        CHECK(text == item.expected);
    }
    const auto invalid = std::to_array<std::string_view>({
        "f\"\"\"inline\"\"\"",
        "f\"\"\"\nA\"\"\"",
        "f\"\"\"\n{0}\"\"\"",
        "f\"\"\"\nA",
        "f\"\"\"\nA\rB\n\"\"\"",
        "f\"\"\"\n\\\n\"\"\"",
        "f\"\"\"\n}\n\"\"\"",
        "f\"\"\"\n\xff\n\"\"\"",
    });
    for (const auto input : invalid) {
        CAPTURE(input);
        const auto result =
            lex(SourceView {
                .source_id = SourceID::from_index(0),
                .text = input,
                .origin = "invalid-multiline.cv",
            });
        CHECK_FALSE(result.diagnostics.empty());
    }
}
