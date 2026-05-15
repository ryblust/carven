import zero.driver.pipeline;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

static auto transpile(std::string_view content) noexcept -> TranspileResult {
    auto driver = Driver();
    return transpile(driver, { .filename = "test.zero", .content = std::string(content) });
}

static auto test_if_expr_ternary() noexcept -> void {
    current_test = "if-expr ternary emission";
    const auto result = transpile("fn main() { if true { 1 } else { 2 } }");
    CHECK(!result.has_errors());
    CHECK(!result.output.contains("if (true)"));
    CHECK(result.output.contains("true ? 1 : 2"));
}

static auto test_if_expr_nested_ternary() noexcept -> void {
    current_test = "nested if-expr ternary";
    const auto result = transpile("fn main() { if true { if false { 1 } else { 2 } } else { 3 } }");
    CHECK(!result.has_errors());
    CHECK(!result.output.contains("if (true)"));
    CHECK(result.output.contains("true ? false ? 1 : 2 : 3"));
}

static auto test_if_without_else_block_form() noexcept -> void {
    current_test = "if without else stays block form";
    const auto result = transpile("fn main() { if true { return 1; } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("if (true)"));
    CHECK(result.output.contains("return 1;"));
}

static auto test_if_multi_stmt_then_blocks_ternary() noexcept -> void {
    current_test = "multi-stmt then blocks ternary";
    const auto result = transpile("fn main() { if true { let x = 1; x } else { 2 } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("if (true)"));
}

static auto test_if_multi_stmt_else_blocks_ternary() noexcept -> void {
    current_test = "multi-stmt else blocks ternary";
    const auto result = transpile("fn main() { if true { 1 } else { let x = 2; x } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("if (true)"));
}

static auto test_if_empty_branches() noexcept -> void {
    current_test = "if with empty branches";
    const auto result = transpile("fn main() { if true { } else { } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("if (true)"));
}

static auto test_if_expr_assign() noexcept -> void {
    current_test = "if-expr used in assignment";
    const auto result = transpile("fn main() { let x = if true { 1 } else { 2 }; }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("true ? 1 : 2"));
}

auto main() noexcept -> int {
    test_if_expr_ternary();
    test_if_expr_nested_ternary();
    test_if_without_else_block_form();
    test_if_multi_stmt_then_blocks_ternary();
    test_if_multi_stmt_else_blocks_ternary();
    test_if_empty_branches();
    test_if_expr_assign();

    return print_test_result();
}
