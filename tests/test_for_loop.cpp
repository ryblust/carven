import zero.driver.pipeline;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

static auto transpile(std::string_view content) noexcept -> TranspileResult {
    auto driver = Driver();
    return transpile(driver, { .filename = "test.zero", .content = std::string(content) });
}

static auto test_for_var_init() noexcept -> void {
    current_test = "for with var init";
    const auto result = transpile("fn main() { for (var i = 0; i < 5; ++i) { } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("for (auto i = 0; i < 5; ++i)"));
}

static auto test_for_let_init() noexcept -> void {
    current_test = "for with let init";
    const auto result = transpile("fn main() { for (let i = 0; i < 5; ++i) { return i; } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("for (const auto i = 0; i < 5; ++i)"));
}

static auto test_for_const_init() noexcept -> void {
    current_test = "for with const init";
    const auto result = transpile("fn main() { for (const i = 0; i < 5; ++i) { } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("for (constexpr auto i = 0; i < 5; ++i)"));
}

static auto test_for_expr_init() noexcept -> void {
    current_test = "for with expression init";
    const auto result = transpile("fn main() { for (i = 0; i < 5; ++i) { } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("for (i = 0; i < 5; ++i)"));
}

static auto test_for_no_init() noexcept -> void {
    current_test = "for with no init";
    const auto result = transpile("fn main() { for (; i < 5; ++i) { } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("for (; i < 5; ++i)"));
}

static auto test_for_empty() noexcept -> void {
    current_test = "empty for loop";
    const auto result = transpile("fn main() { for (;;) { } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("for (; ; )"));
}

static auto test_for_nested() noexcept -> void {
    current_test = "nested for loops";
    const auto result = transpile("fn main() { for (var x = 0; x < 10; ++x) { for (var y = 0; y < 10; ++y) { } } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("for (auto x = 0; x < 10; ++x)"));
    CHECK(result.output.contains("for (auto y = 0; y < 10; ++y)"));
}

static auto test_for_typed_init() noexcept -> void {
    current_test = "for with typed init";
    const auto result = transpile("fn main() { for (var i: i32 = 0; i < 5; ++i) { } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("for (auto i = std::int32_t(0); i < 5; ++i)"));
}

static auto test_for_block_body() noexcept -> void {
    current_test = "for with block body";
    const auto result = transpile("fn main() { for (var i = 0; i < 3; ++i) { let x = i; } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("{\n        const auto x = i;\n    }\n"));
}

static auto test_for_single_stmt_body() noexcept -> void {
    current_test = "for with single statement body";
    const auto result = transpile("fn main() { for (var i = 0; i < 3; ++i) return i; }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("return i;"));
}

auto main() noexcept -> int {
    test_for_var_init();
    test_for_let_init();
    test_for_const_init();
    test_for_expr_init();
    test_for_no_init();
    test_for_empty();
    test_for_nested();
    test_for_typed_init();
    test_for_block_body();
    test_for_single_stmt_body();

    return print_test_result();
}
