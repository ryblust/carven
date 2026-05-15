import zero.frontend.token;
import zero.frontend.lexer;
import zero.frontend.parser.ast;
import zero.frontend.parser;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

static auto test_missing_semicolon() noexcept -> void {
    current_test = "missing semicolon mid-block";
    const auto result = parse(tokenize("fn main() { let x = 1 let y = 2; }"), "fn main() { let x = 1 let y = 2; }");
    CHECK(result.has_errors());
    CHECK(!result.items.empty());
}

static auto test_unexpected_top_level() noexcept -> void {
    current_test = "unexpected token at top level";
    const auto result = parse(tokenize("@ fn foo() {}"), "@ fn foo() {}");
    CHECK(result.has_errors());
    CHECK(!result.items.empty());
}

static auto test_bad_expression_in_function() noexcept -> void {
    current_test = "bad expression recovery";
    const auto result = parse(tokenize("fn main() { let x = @; return 1; }"), "fn main() { let x = @; return 1; }");
    CHECK(result.has_errors());
    CHECK(!result.items.empty());
}

static auto test_missing_closing_brace() noexcept -> void {
    current_test = "missing closing brace";
    const auto result = parse(tokenize("fn main() { let x = 1;"), "fn main() { let x = 1;");
    CHECK(result.has_errors());
    // error is reported, parser does not crash
    // parse_block fails without closing brace
}

static auto test_extra_closing_brace() noexcept -> void {
    current_test = "extra closing brace";
    const auto result = parse(tokenize("fn main() { let x = 1; } }"), "fn main() { let x = 1; } }");
    CHECK(result.has_errors());
    CHECK(!result.items.empty());
}

static auto test_garbage_between_functions() noexcept -> void {
    current_test = "garbage between functions";
    const auto source = std::string_view("fn foo() {} @@@ fn bar() {}");
    const auto result = parse(tokenize(source), source);
    CHECK(result.has_errors());
    CHECK_EQ(result.items.size(), 2uz);
}

static auto test_bad_for_init() noexcept -> void {
    current_test = "bad for-loop init recovery";
    const auto source = std::string_view("fn main() { for (@; i < 5; ++i) { } }");
    const auto result = parse(tokenize(source), source);
    CHECK(result.has_errors());
    CHECK(!result.items.empty());
}

static auto test_bad_while_condition() noexcept -> void {
    current_test = "bad while condition recovery";
    const auto source = std::string_view("fn main() { while @ { } }");
    const auto result = parse(tokenize(source), source);
    CHECK(result.has_errors());
    CHECK(!result.items.empty());
}

static auto test_unexpected_token_in_expr() noexcept -> void {
    current_test = "unexpected token in expression";
    const auto result = parse_expr_tokens(tokenize("1 + @ + 2"), "1 + @ + 2");
    CHECK(result.has_errors());
    CHECK(!result.exprs.empty());
}

static auto test_unterminated_block() noexcept -> void {
    current_test = "unterminated block";
    const auto result = parse(tokenize("fn main() { if true { return 1; }"), "fn main() { if true { return 1; }");
    CHECK(result.has_errors());
    // error is reported, parser does not crash
}

auto main() noexcept -> int {
    test_missing_semicolon();
    test_unexpected_top_level();
    test_bad_expression_in_function();
    test_missing_closing_brace();
    test_extra_closing_brace();
    test_garbage_between_functions();
    test_bad_for_init();
    test_bad_while_condition();
    test_unexpected_token_in_expr();
    test_unterminated_block();

    return print_test_result();
}
