module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.lex.literals;

import :frontend.lex;
import :frontend.lex.literal;
import :frontend.lex.token;
import :frontend.literal;
import :source.identifier;
import :source.text;
import :test.internal.frontend.lex.fixture;
import std;

TEST_CASE("Lexer: numeric spellings follow the grammar exactly") {
    static constexpr auto valid = std::to_array<std::string_view>({
        "0",     "42",         "42i8",    "42i16",         "42i32",   "42i64",     "42u8",
        "42u16", "42u32",      "42u64",   "42isize",       "42usize", "1f32",      "1f64",
        "3.14",  "3.14e-2f32", "1E+9f64", "0xDeadBEEFu64", "0x1f32",  "0B1010i16", "0o755usize",
    });
    for (const auto& spelling : valid) {
        check_token(spelling, TokenKind::NumberLiteral);
    }

    static constexpr auto not_single_tokens = std::to_array<std::string_view>({
        ".5",
        "0.",
    });
    for (const auto& spelling : not_single_tokens) {
        check_not_single_number(spelling);
    }

    static constexpr auto malformed = std::to_array<std::string_view>({
        "1e",
        "0x",
        "0b102",
        "0o8",
        "1f",
        "1ul",
        "1.0i32",
    });
    for (const auto& spelling : malformed) {
        check_lexical_error(spelling);
    }
}

TEST_CASE("Lexer: strings and characters accept only shared simple escapes") {
    static constexpr auto valid_characters = std::to_array<std::string_view>({
        "'a'",
        "'\\''",
        "'\\\"'",
        "'\\\\'",
        "'\\n'",
        "'\\t'",
        "'\\r'",
        "'\\0'",
        "'é'",
        "'你'",
        "'😀'",
        "'\\u{1F600}'",
    });
    static constexpr auto valid_strings = std::to_array<std::string_view>({
        "\"\"",
        "\"hello\"",
        "\"quote: \\\"\"",
        "\"slash: \\\\\"",
        "\"\\'\\n\\t\\r\\0\"",
        "\"你好\"",
    });
    for (const auto& spelling : valid_characters) {
        check_token(spelling, TokenKind::CharLiteral);
    }
    for (const auto& spelling : valid_strings) {
        check_token(spelling, TokenKind::StringLiteral);
    }

    static constexpr auto invalid = std::to_array<std::string_view>({
        "''",
        "'ab'",
        "'\\q'",
        "'é'",
        "'\\u{}'",
        "'\\u{D800}'",
        "'\\u{110000}'",
        "'\\x61'",
        "\"\\q\"",
        "\"\\01\"",
        "'\\07'",
        "'a\n'",
        "\"line\nbreak\"",
        "\"unterminated",
    });
    for (const auto& spelling : invalid) {
        check_lexical_error(spelling);
    }
}

TEST_CASE("Lexer: C++ source fragments are line-fenced opaque tokens") {
    static constexpr auto valid = std::to_array<std::string_view>({
        "#[cpp] ---\n---",
        "#[cpp] ---\n{ if (ready) { call(); } }\n---",
        R"CV(#[cpp] ---
auto text = "}";
auto raw = R"tag({ // not Carven })tag";
---)CV",
        "#[cpp] ---\n// }\n/* { */\n#if 0\n}\n#endif\n---",
        "#[cpp] -----\n---\n-----",
        "#[cpp]\t---\r\nauto value = 1;\r\n\t---\t",
    });
    for (const auto& spelling : valid) {
        check_token(spelling, TokenKind::CppSourceFragment);
    }

    static constexpr auto invalid = std::to_array<std::string_view>({
        "#[ cpp] ---\n---",
        "#[cpp ] ---\n---",
        "#[cpp]",
        "#[cpp] --\n--",
        "#[cpp] --- trailing\n---",
        "#[cpp] ---\nnative();",
        "#[cpp] ----\nnative();\n---",
    });
    for (const auto& spelling : invalid) {
        check_lexical_error(spelling);
    }

    static constexpr auto early_close = std::string_view(
        "#[cpp] ---\n"
        "before();\n"
        "---\n"
        "after();\n"
    );
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = early_close,
        .origin = "early-close.cv",
    };
    const auto lexed = lex(source);
    REQUIRE(lexed.diagnostics.empty());
    REQUIRE(!lexed.value.tokens().empty());
    CHECK_EQ(lexed.value.tokens().front().kind, TokenKind::CppSourceFragment);
    CHECK_EQ(slice(early_close, lexed.value.tokens().front().span), "#[cpp] ---\nbefore();\n---");
}

TEST_CASE("Lexer: C++ header names retain their dedicated spelling") {
    static constexpr auto text =
        std::string_view("import <vendor/api.hpp>; import \"native/provider.hpp\";");
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "header-token-test.cv",
    };
    const auto lexed = lex(source);
    REQUIRE(lexed.diagnostics.empty());
    const auto tokens = lexed.value.tokens();
    REQUIRE_EQ(tokens.size(), 6u);
    CHECK_EQ(tokens[1].kind, TokenKind::CppAngleHeaderName);
    CHECK_EQ(slice(text, tokens[1].span), "<vendor/api.hpp>");
    CHECK_EQ(tokens[4].kind, TokenKind::CppQuoteHeaderName);
    CHECK_EQ(slice(text, tokens[4].span), "\"native/provider.hpp\"");

    check_lexical_error("import <>;");
    check_lexical_error("import \"\";");
    check_lexical_error("import <unterminated;");
    check_lexical_error("import \"unterminated;");
}

TEST_CASE("Lexer: numeric scanner exposes typed values and error facts") {
    const auto number = scan_numeric_literal("42u8 rest", 10u);
    REQUIRE(number.has_value());
    CHECK_EQ(number->consumed, 4uz);
    const auto& integer = std::get<IntegerLiteralValue>(number->value);
    CHECK_EQ(integer.magnitude, 42u);
    CHECK_EQ(integer.suffix, NumericSuffix::U8);

    const auto invalid_number = scan_numeric_literal("0xg", 0u);
    REQUIRE(!invalid_number.has_value());
    CHECK_EQ(invalid_number.error().consumed, 3uz);
    CHECK_EQ(invalid_number.error().error_offset, 2uz);
    CHECK(invalid_number.error().has_base_prefix);
}

TEST_CASE("Lexer: string scanner exposes typed values and error facts") {
    const auto string = scan_string_literal("\"a\\n\" rest");
    REQUIRE(string.has_value());
    CHECK_EQ(string->consumed, 5uz);
    CHECK_EQ(string->value.bytes, "a\n");

    const auto invalid_string = scan_string_literal("\"\\q\"");
    REQUIRE(!invalid_string.has_value());
    CHECK_EQ(invalid_string.error().consumed, 4uz);
    CHECK_EQ(invalid_string.error().error_offset, 1uz);
}

TEST_CASE("Lexer: character scanner exposes typed values and error facts") {
    const auto character = scan_character_literal("'x' rest");
    REQUIRE(character.has_value());
    CHECK_EQ(character->consumed, 3uz);
    CHECK_EQ(character->value.scalar, U'x');

    const auto invalid_character = scan_character_literal("''");
    REQUIRE(!invalid_character.has_value());
    CHECK_EQ(invalid_character.error().consumed, 2uz);
    CHECK_EQ(invalid_character.error().error_offset, 1uz);
}

TEST_CASE("Lexer: numeric tokens carry converted values") {
    static constexpr auto spellings = std::to_array<std::string_view>({
        "0",
        "42i32",
        "255u8",
        "1.5",
        "1e10",
        "1e-3f64",
        "7f32",
        "0xffu64",
        "0B101i8",
        "0o77usize",
    });
    for (const auto& spelling : spellings) {
        const auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = spelling,
            .origin = "tokenize-test.cv",
        };
        const auto lexed = lex(source);
        CAPTURE(spelling);
        REQUIRE(lexed.diagnostics.empty());
        REQUIRE_EQ(lexed.value.tokens().size(), 1u);
        const auto& token = lexed.value.tokens().front();
        CHECK_EQ(token.kind, TokenKind::NumberLiteral);
        const auto& value = lexed.value.literal_value(0uz);
        const auto* integer = std::get_if<IntegerLiteralValue>(&value);
        const auto* floating = std::get_if<FloatingLiteralValue>(&value);
        REQUIRE((integer != nullptr || floating != nullptr));
        CHECK_EQ(
            integer != nullptr ? integer->conversion : floating->conversion,
            NumericConversion::Exact
        );
    }
}

TEST_CASE("Lexer: quoted tokens carry decoded values") {
    const auto string_source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "\"a\\n\\u{4e09}\"",
        .origin = "tokenize-test.cv",
    };
    const auto string = lex(string_source);
    REQUIRE(string.diagnostics.empty());
    REQUIRE_EQ(string.value.tokens().size(), 1u);
    const auto& string_value = std::get<StringLiteralValue>(string.value.literal_value(0uz));
    CHECK_EQ(string_value.bytes, "a\n三");

    const auto character_source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "'\\u{1f600}'",
        .origin = "tokenize-test.cv",
    };
    const auto character = lex(character_source);
    REQUIRE(character.diagnostics.empty());
    const auto& character_value =
        std::get<CharacterLiteralValue>(character.value.literal_value(0uz));
    CHECK_EQ(character_value.scalar, U'😀');

    const auto invalid_utf8_character = std::string("'\xff'", 3);
    const auto invalid_utf8_string = std::string("\"\xff\"", 3);
    check_lexical_error(invalid_utf8_character);
    check_lexical_error(invalid_utf8_string);
}

TEST_CASE("Lexer: stored literal value associations follow token positions") {
    static constexpr auto text = std::string_view("let value = 42; false \"text\" true 'x'");
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto lexed = lex(source);
    REQUIRE(lexed.diagnostics.empty());
    const auto tokens = lexed.value.tokens();
    REQUIRE_EQ(tokens.size(), 9u);
    REQUIRE_EQ(tokens[3].kind, TokenKind::NumberLiteral);
    REQUIRE_EQ(tokens[5].kind, TokenKind::False);
    REQUIRE_EQ(tokens[6].kind, TokenKind::StringLiteral);
    REQUIRE_EQ(tokens[7].kind, TokenKind::True);
    REQUIRE_EQ(tokens[8].kind, TokenKind::CharLiteral);

    const auto& number = std::get<IntegerLiteralValue>(lexed.value.literal_value(3uz));
    const auto& string = std::get<StringLiteralValue>(lexed.value.literal_value(6uz));
    const auto& character = std::get<CharacterLiteralValue>(lexed.value.literal_value(8uz));
    CHECK_EQ(number.magnitude, 42u);
    CHECK_EQ(string.bytes, "text");
    CHECK_EQ(character.scalar, U'x');
}
