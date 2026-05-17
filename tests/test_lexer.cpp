import zero.common.source;
import zero.frontend.token;
import zero.frontend.lexer;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

using enum TokenKind;

static auto tokens_match(std::span<const Token> tokens, std::span<const TokenKind> expected) noexcept -> bool {
    if (tokens.size() != expected.size()) {
        std::println("  token count mismatch: expected {}, got {}", expected.size(), tokens.size());
        return false;
    }
    for (auto i = 0uz; i < tokens.size(); ++i) {
        if (tokens[i].kind != expected[i]) {
            std::println("  mismatch at index {}: expected {}, got {}",
                i, std::format("{}", expected[i]), std::format("{}", tokens[i].kind));
            return false;
        }
    }
    return true;
}

static auto test_empty_input() noexcept -> void {
    current_test = "empty input";
    CHECK(tokenize("").empty());
}

static auto test_whitespace_only() noexcept -> void {
    current_test = "whitespace only";
    CHECK(tokenize("  \t\n\r  ").empty());
}

static auto test_single_char_punctuation() noexcept -> void {
    current_test = "single-char punctuation";

    const auto tokens = tokenize("()[]{},.;:");
    const auto expected = std::vector<TokenKind> {
        LeftParen, RightParen, LeftBracket, RightBracket,
        LeftBrace, RightBrace, Comma, Dot, SemiColon, Colon
    };
    CHECK(tokens_match(tokens, expected));
}

static auto test_single_char_operators() noexcept -> void {
    current_test = "single-char operators";

    const auto tokens = tokenize("+-*/%");
    const auto expected = std::vector<TokenKind>{ Plus, Minus, Star, Slash, Percent };
    CHECK(tokens_match(tokens, expected));
}

static auto test_compound_assignment_operators() noexcept -> void {
    current_test = "compound assignment operators";

    const auto tokens = tokenize("+= -= *= /= %= &= |= ^= <<= >>=");
    const auto expected = std::vector<TokenKind> {
        PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
        AmpersandEqual, PipeEqual, CaretEqual, LeftShiftEqual, RightShiftEqual
    };
    CHECK(tokens_match(tokens, expected));
}

static auto test_increment_decrement() noexcept -> void {
    current_test = "increment / decrement";

    const auto tokens = tokenize("++ --");
    const auto expected = std::vector<TokenKind>{ PlusPlus, MinusMinus };
    CHECK(tokens_match(tokens, expected));
}

static auto test_comparison_operators() noexcept -> void {
    current_test = "comparison operators";

    const auto tokens = tokenize("== != <= >= < >");
    const auto expected = std::vector<TokenKind> {
        EqualEqual, BangEqual, LessEqual, GreaterEqual, Less, Greater
    };
    CHECK(tokens_match(tokens, expected));
}

static auto test_logical_operators() noexcept -> void {
    current_test = "logical operators";

    const auto tokens = tokenize("&& || !");
    const auto expected = std::vector<TokenKind>{ AmpersandAmpersand, PipePipe, Bang };
    CHECK(tokens_match(tokens, expected));
}

static auto test_bitwise_shift_operators() noexcept -> void {
    current_test = "bitwise / shift operators";

    const auto tokens = tokenize("& | ^ ~ << >>");
    const auto expected = std::vector<TokenKind> {
        Ampersand, Pipe, Caret, Tilde, LeftShift, RightShift
    };
    CHECK(tokens_match(tokens, expected));
}

static auto test_arrow_operators() noexcept -> void {
    current_test = "arrow operators";

    const auto tokens = tokenize("-> =>");
    const auto expected = std::vector<TokenKind>{ Arrow, FatArrow };
    CHECK(tokens_match(tokens, expected));
}

static auto test_colon_colon() noexcept -> void {
    current_test = "colon colon";

    const auto tokens = tokenize("::");
    const auto expected = std::vector<TokenKind>{ ColonColon };
    CHECK(tokens_match(tokens, expected));
}

static auto test_keywords() noexcept -> void {
    current_test = "keywords";

    const auto tokens = tokenize("import export using enum struct fn var let const as if else while for return true false");
    // Every one of these should be a Keyword token
    CHECK_EQ(tokens.size(), 17uz);
    for (const auto token : tokens) {
        CHECK_EQ(token.kind, Keyword);
    }
}

static auto test_identifiers() noexcept -> void {
    current_test = "identifiers";

    const auto tokens = tokenize("foo bar_baz x1 _leading CamelCase");
    CHECK_EQ(tokens.size(), 5uz);
    for (const auto token : tokens) {
        CHECK_EQ(token.kind, Identifier);
    }
}

static auto test_number_literals() noexcept -> void {
    current_test = "number literals";

    const auto tokens = tokenize("0 42 123 3.14 0.5");
    CHECK_EQ(tokens.size(), 5uz);
    for (const auto token : tokens) {
        CHECK_EQ(token.kind, NumberLiteral);
    }
}

static auto test_string_literals() noexcept -> void {
    current_test = "string literals";

    const auto tokens = tokenize(R"("hello" "world" "esc\\" "quote\"")");
    CHECK_EQ(tokens.size(), 4uz);
    for (const auto token : tokens) {
        CHECK_EQ(token.kind, StringLiteral);
    }
}

static auto test_char_literals() noexcept -> void {
    current_test = "char literals";

    const auto tokens = tokenize(R"('a' 'Z' '\\' '\'')");
    CHECK_EQ(tokens.size(), 4uz);
    for (const auto token : tokens) {
        CHECK_EQ(token.kind, CharLiteral);
    }
}

static auto test_comments_are_skipped() noexcept -> void {
    current_test = "comments are skipped";

    const auto tokens = tokenize("x // this is a comment\ny");
    const auto expected = std::vector<TokenKind>{ Identifier, Identifier };
    CHECK(tokens_match(tokens, expected));
}

static auto test_comment_at_eof() noexcept -> void {
    current_test = "comment at EOF";

    const auto tokens = tokenize("x // no newline");
    const auto expected = std::vector<TokenKind>{ Identifier };
    CHECK(tokens_match(tokens, expected));
}

static auto test_mixed_input() noexcept -> void {
    current_test = "mixed input";

    const auto tokens = tokenize("import std;");
    const auto expected = std::vector<TokenKind>{ Keyword, Identifier, SemiColon };
    CHECK(tokens_match(tokens, expected));
}

static auto test_function_decl() noexcept -> void {
    current_test = "function declaration";

    const auto tokens = tokenize("fn main() -> i32 {}");
    const auto expected = std::vector<TokenKind> {
        Keyword, Identifier, LeftParen, RightParen, Arrow, Identifier, LeftBrace, RightBrace
    };
    CHECK(tokens_match(tokens, expected));
}

static auto test_span_positions() noexcept -> void {
    current_test = "span positions";

    const auto source = std::string_view("ab cd");
    const auto tokens = tokenize(source);

    CHECK_EQ(tokens.size(), 2uz);
    CHECK_EQ(tokens[0].span.start, 0u);
    CHECK_EQ(tokens[0].span.end, 2u);
    CHECK_EQ(tokens[1].span.start, 3u);
    CHECK_EQ(tokens[1].span.end, 5u);

    CHECK_EQ(text_at(source, tokens[0].span), "ab");
    CHECK_EQ(text_at(source, tokens[1].span), "cd");
}

static auto test_equal_sign() noexcept -> void {
    current_test = "equal sign";
    const auto tokens = tokenize("x = 5");
    const auto expected = std::vector<TokenKind>{ Identifier, Equal, NumberLiteral };
    CHECK(tokens_match(tokens, expected));
}

static auto test_unknown_character() noexcept -> void {
    current_test = "unknown character";
    const auto tokens = tokenize("@ # $");
    CHECK_EQ(tokens.size(), 3uz);
    for (const auto token : tokens) {
        CHECK_EQ(token.kind, Unknown);
    }
}

static auto test_unterminated_string() noexcept -> void {
    current_test = "unterminated string";
    const auto tokens = tokenize("\"hello");
    CHECK_EQ(tokens.size(), 1uz);
    CHECK_EQ(tokens[0].kind, StringLiteral);
}

static auto test_unterminated_char() noexcept -> void {
    current_test = "unterminated char";
    const auto tokens = tokenize("'a");
    CHECK_EQ(tokens.size(), 1uz);
    CHECK_EQ(tokens[0].kind, CharLiteral);
}

auto main() noexcept -> int {
    test_empty_input();
    test_whitespace_only();
    test_single_char_punctuation();
    test_single_char_operators();
    test_compound_assignment_operators();
    test_increment_decrement();
    test_comparison_operators();
    test_logical_operators();
    test_bitwise_shift_operators();
    test_arrow_operators();
    test_colon_colon();
    test_keywords();
    test_identifiers();
    test_number_literals();
    test_string_literals();
    test_char_literals();
    test_comments_are_skipped();
    test_comment_at_eof();
    test_mixed_input();
    test_function_decl();
    test_span_positions();
    test_equal_sign();
    test_unknown_character();
    test_unterminated_string();
    test_unterminated_char();

    return print_test_result();
}
