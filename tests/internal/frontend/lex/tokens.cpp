module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.frontend.lex.tokens;

import :frontend.lex;
import :frontend.lex.token;
import :source.identifier;
import :source.text;
import :test.internal.frontend.lex.fixture;
import std;

static_assert(!std::copy_constructible<TokenBuffer>);
static_assert(std::movable<TokenBuffer>);

TEST_CASE("Lexer: every reserved spelling has its grammar token") {
    static constexpr auto cases = std::to_array<TokenCase>({
        {.spelling = "as", .kind = TokenKind::As},
        {.spelling = "break", .kind = TokenKind::Break},
        {.spelling = "const", .kind = TokenKind::Const},
        {.spelling = "continue", .kind = TokenKind::Continue},
        {.spelling = "else", .kind = TokenKind::Else},
        {.spelling = "enum", .kind = TokenKind::Enum},
        {.spelling = "export", .kind = TokenKind::Export},
        {.spelling = "false", .kind = TokenKind::False},
        {.spelling = "fn", .kind = TokenKind::Fn},
        {.spelling = "for", .kind = TokenKind::For},
        {.spelling = "if", .kind = TokenKind::If},
        {.spelling = "import", .kind = TokenKind::Import},
        {.spelling = "in", .kind = TokenKind::In},
        {.spelling = "is", .kind = TokenKind::Is},
        {.spelling = "let", .kind = TokenKind::Let},
        {.spelling = "match", .kind = TokenKind::Match},
        {.spelling = "private", .kind = TokenKind::Private},
        {.spelling = "return", .kind = TokenKind::Return},
        {.spelling = "rethrow", .kind = TokenKind::Rethrow},
        {.spelling = "struct", .kind = TokenKind::Struct},
        {.spelling = "test", .kind = TokenKind::Test},
        {.spelling = "throw", .kind = TokenKind::Throw},
        {.spelling = "true", .kind = TokenKind::True},
        {.spelling = "try", .kind = TokenKind::Try},
        {.spelling = "catch", .kind = TokenKind::Catch},
        {.spelling = "using", .kind = TokenKind::Using},
        {.spelling = "var", .kind = TokenKind::Var},
        {.spelling = "while", .kind = TokenKind::While},
    });
    check_tokens(cases);
}

TEST_CASE("Lexer: identifiers are ASCII") {
    static constexpr auto identifiers = std::to_array<std::string_view>({
        "name",
        "_",
        "_private",
        "__value",
        "__carven_temporary",
        "value_42",
        "imported",
        "new",
        "delete",
        "nullptr",
    });
    for (const auto& spelling : identifiers) {
        check_token(spelling, TokenKind::Identifier);
    }

    check_lexical_error("变量");
    check_lexical_error(std::string_view("\xff", 1));
}

TEST_CASE("Lexer: throw syntax reserves singular keywords only") {
    check_token("throws", TokenKind::Identifier);
    check_token_sequence(
        "??",
        std::to_array<TokenCase>({
            {.spelling = "?", .kind = TokenKind::Question},
            {.spelling = "?", .kind = TokenKind::Question},
        })
    );
}

TEST_CASE("Lexer: shared identifier classification covers complete spellings") {
    static constexpr auto ordinary_ids = std::to_array<std::string_view>({
        "name",
        "_",
        "__value",
        "__carven_module",
        "value_42",
        "module_42",
        "cv",
        "craft",
    });
    for (const auto& spelling : ordinary_ids) {
        CHECK(std::holds_alternative<OrdinaryIdentifier>(classify_identifier(spelling)));
    }

    static constexpr auto keyword_ids = std::to_array<std::string_view>({
        "import",
        "match",
        "private",
        "while",
    });
    for (const auto& spelling : keyword_ids) {
        CHECK(std::holds_alternative<KeywordIdentifier>(classify_identifier(spelling)));
    }

    static constexpr auto invalid_ids = std::to_array<std::string_view>({
        "",
        "42name",
        "42module",
        "hyphen-name",
        "dot.name",
        "变量",
    });
    for (const auto& spelling : invalid_ids) {
        CHECK(std::holds_alternative<InvalidIdentifier>(classify_identifier(spelling)));
    }
}

TEST_CASE("Lexer: punctuators use maximal munch") {
    static constexpr auto cases = std::to_array<TokenCase>({
        {.spelling = "(", .kind = TokenKind::LeftParen},
        {.spelling = ")", .kind = TokenKind::RightParen},
        {.spelling = "[", .kind = TokenKind::LeftBracket},
        {.spelling = "]", .kind = TokenKind::RightBracket},
        {.spelling = "{", .kind = TokenKind::LeftBrace},
        {.spelling = "}", .kind = TokenKind::RightBrace},
        {.spelling = ",", .kind = TokenKind::Comma},
        {.spelling = ".", .kind = TokenKind::Dot},
        {.spelling = "..", .kind = TokenKind::DotDot},
        {.spelling = ":", .kind = TokenKind::Colon},
        {.spelling = ";", .kind = TokenKind::Semicolon},
        {.spelling = "+", .kind = TokenKind::Plus},
        {.spelling = "-", .kind = TokenKind::Minus},
        {.spelling = "*", .kind = TokenKind::Star},
        {.spelling = "/", .kind = TokenKind::Slash},
        {.spelling = "%", .kind = TokenKind::Percent},
        {.spelling = "!", .kind = TokenKind::Bang},
        {.spelling = "=", .kind = TokenKind::Equal},
        {.spelling = "<", .kind = TokenKind::Less},
        {.spelling = ">", .kind = TokenKind::Greater},
        {.spelling = "&", .kind = TokenKind::Ampersand},
        {.spelling = "|", .kind = TokenKind::Pipe},
        {.spelling = "^", .kind = TokenKind::Caret},
        {.spelling = "~", .kind = TokenKind::Tilde},
        {.spelling = "+=", .kind = TokenKind::PlusEqual},
        {.spelling = "-=", .kind = TokenKind::MinusEqual},
        {.spelling = "*=", .kind = TokenKind::StarEqual},
        {.spelling = "/=", .kind = TokenKind::SlashEqual},
        {.spelling = "%=", .kind = TokenKind::PercentEqual},
        {.spelling = "!=", .kind = TokenKind::BangEqual},
        {.spelling = "==", .kind = TokenKind::EqualEqual},
        {.spelling = "<=", .kind = TokenKind::LessEqual},
        {.spelling = ">=", .kind = TokenKind::GreaterEqual},
        {.spelling = "++", .kind = TokenKind::PlusPlus},
        {.spelling = "--", .kind = TokenKind::MinusMinus},
        {.spelling = "&&", .kind = TokenKind::AmpersandAmpersand},
        {.spelling = "||", .kind = TokenKind::PipePipe},
        {.spelling = "<<", .kind = TokenKind::LeftShift},
        {.spelling = ">>", .kind = TokenKind::RightShift},
        {.spelling = "&=", .kind = TokenKind::AmpersandEqual},
        {.spelling = "|=", .kind = TokenKind::PipeEqual},
        {.spelling = "^=", .kind = TokenKind::CaretEqual},
        {.spelling = "<<=", .kind = TokenKind::LeftShiftEqual},
        {.spelling = ">>=", .kind = TokenKind::RightShiftEqual},
        {.spelling = "->", .kind = TokenKind::Arrow},
        {.spelling = "=>", .kind = TokenKind::FatArrow},
        {.spelling = "::", .kind = TokenKind::ColonColon},
    });
    check_tokens(cases);
}

TEST_CASE("Lexer: range punctuation preserves numbers and member access") {
    static constexpr auto integer_range = std::to_array<TokenCase>({
        {.spelling = "1", .kind = TokenKind::NumberLiteral},
        {.spelling = "..", .kind = TokenKind::DotDot},
        {.spelling = "10", .kind = TokenKind::NumberLiteral},
    });
    check_token_sequence("1..10", integer_range);
    check_token_sequence("1 .. 10", integer_range);

    check_token_sequence(
        "1 . . 10",
        std::to_array<TokenCase>({
            {.spelling = "1", .kind = TokenKind::NumberLiteral},
            {.spelling = ".", .kind = TokenKind::Dot},
            {.spelling = ".", .kind = TokenKind::Dot},
            {.spelling = "10", .kind = TokenKind::NumberLiteral},
        })
    );
    check_token_sequence(
        "1.5..10",
        std::to_array<TokenCase>({
            {.spelling = "1.5", .kind = TokenKind::NumberLiteral},
            {.spelling = "..", .kind = TokenKind::DotDot},
            {.spelling = "10", .kind = TokenKind::NumberLiteral},
        })
    );
    check_token_sequence(
        "value.member",
        std::to_array<TokenCase>({
            {.spelling = "value", .kind = TokenKind::Identifier},
            {.spelling = ".", .kind = TokenKind::Dot},
            {.spelling = "member", .kind = TokenKind::Identifier},
        })
    );
}

TEST_CASE("Lexer: discarded text does not disturb source spans") {
    static constexpr auto text = std::string_view(" \t// first\r\nlet\nvalue");
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    REQUIRE(result.diagnostics.empty());
    const auto tokens = result.value.tokens();
    REQUIRE_EQ(tokens.size(), 2u);
    CHECK_EQ(tokens[0].kind, TokenKind::Let);
    CHECK_EQ(slice(text, tokens[0].span), "let");
    CHECK_EQ(tokens[1].kind, TokenKind::Identifier);
    CHECK_EQ(slice(text, tokens[1].span), "value");
}

TEST_CASE("Lexer: multiple lexical errors do not require an end sentinel") {
    static constexpr auto text = std::string_view("@ let value = 1; $");
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);

    CHECK_EQ(result.diagnostics.size(), 2u);
    CHECK_EQ(result.diagnostics[0].finding.message, "unknown source character");
    REQUIRE(result.diagnostics[0].attachment.primary.has_value());
    CHECK_EQ(result.diagnostics[0].attachment.primary->span.span.start(), 0u);
    CHECK_EQ(result.diagnostics[0].attachment.primary->span.span.end(), 1u);
    CHECK_EQ(result.diagnostics[1].finding.message, "unknown source character");
    REQUIRE(result.diagnostics[1].attachment.primary.has_value());
    CHECK_EQ(result.diagnostics[1].attachment.primary->span.span.start(), 17u);
    CHECK_EQ(result.diagnostics[1].attachment.primary->span.span.end(), 18u);
    const auto tokens = result.value.tokens();
    REQUIRE_EQ(tokens.size(), 7u);
    CHECK_EQ(tokens.front().kind, TokenKind::Invalid);
    CHECK_EQ(tokens.back().kind, TokenKind::Invalid);
    CHECK_EQ(slice(text, tokens[1].span), "let");
}

TEST_CASE("Lexer: tokenizes a declaration at runtime") {
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "let answer = 42i32;",
        .origin = "tokenize-test.cv",
    };
    const auto result = lex(source);
    REQUIRE(result.diagnostics.empty());
    const auto tokens = result.value.tokens();
    REQUIRE_EQ(tokens.size(), 5u);
    CHECK_EQ(tokens[3].kind, TokenKind::NumberLiteral);
}
