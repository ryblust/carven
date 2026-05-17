import zero.common.source;
import zero.frontend.token;
import zero.frontend.lexer;
import zero.frontend.ast;
import zero.frontend.parser;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

static auto test_unicode_in_string() noexcept -> void {
    current_test = "unicode in string literal";
    const auto tokens = tokenize("\"café\"");
    CHECK_EQ(tokens.size(), 1uz);
    CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
}

static auto test_non_ascii_identifier() noexcept -> void {
    current_test = "non-ASCII in identifier";
    const auto tokens = tokenize("café");
    CHECK(tokens.size() >= 2uz);
    CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
}

static auto test_empty_input() noexcept -> void {
    current_test = "empty input";
    CHECK(tokenize("").empty());
}

static auto test_whitespace_only_input() noexcept -> void {
    current_test = "whitespace only";
    CHECK(tokenize("   \n\t  ").empty());
}

static auto test_comment_only_input() noexcept -> void {
    current_test = "comment only";
    CHECK(tokenize("// just a comment").empty());
}

static auto test_single_semicolon_parse() noexcept -> void {
    current_test = "single semicolon parse";
    const auto result = parse(tokenize("fn main() { ; }"), "fn main() { ; }");
    CHECK(!result.has_errors());
    CHECK(!result.items.empty());
}

static auto test_deep_binary_expr() noexcept -> void {
    current_test = "deep binary expression";
    auto source = std::string("1");
    for (auto i = 2; i <= 50; ++i) {
        source.append(" + ").append(std::to_string(i));
    }
    auto wrapped = std::string("fn __t() { ") + source + "; }";
    const auto result = parse(tokenize(wrapped), wrapped);
    CHECK(!result.has_errors());
}

static auto test_deep_nested_blocks() noexcept -> void {
    current_test = "deep nested blocks";
    auto source = std::string("fn main() {");
    for (auto i = 0; i < 10; ++i) {
        source.append(" if true {");
    }
    source.append(" return 1;");
    for (auto i = 0; i < 10; ++i) {
        source.append(" }");
    }
    source.append("}");
    const auto result = parse(tokenize(source), source);
    CHECK(!result.has_errors());
    CHECK(!result.items.empty());
}

static auto test_long_identifier() noexcept -> void {
    current_test = "long identifier";
    const auto id = std::string(500, 'x');
    const auto tokens = tokenize(id);
    CHECK_EQ(tokens.size(), 1uz);
    CHECK_EQ(tokens[0].kind, TokenKind::Identifier);
}

static auto test_long_string_literal() noexcept -> void {
    current_test = "long string literal";
    const auto content = std::string(1000, 'a');
    const auto source = std::string("\"") + content + "\"";
    const auto tokens = tokenize(source);
    CHECK_EQ(tokens.size(), 1uz);
    CHECK_EQ(tokens[0].kind, TokenKind::StringLiteral);
}

auto main() noexcept -> int {
    test_unicode_in_string();
    test_non_ascii_identifier();
    test_empty_input();
    test_whitespace_only_input();
    test_comment_only_input();
    test_single_semicolon_parse();
    test_deep_binary_expr();
    test_deep_nested_blocks();
    test_long_identifier();
    test_long_string_literal();

    return print_test_result();
}
