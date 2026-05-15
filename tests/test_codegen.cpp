import zero.driver.handler;
import zero.driver.pipeline;
import zero.tests.utils;
import std;

#define CHECK(expr)     check((expr), #expr)
#define CHECK_EQ(a, e)  check_eq((a), (e), #a, #e)

static auto transpile(std::string_view content) noexcept -> TranspileResult {
    auto driver = Driver();
    return transpile(driver, { .filename = "test.zero", .content = std::string(content) });
}

static auto test_empty_function() noexcept -> void {
    current_test = "empty function";
    const auto result = transpile("fn main() {}");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("auto main() noexcept -> int {\n}\n"));
}

static auto test_function_with_typed_params() noexcept -> void {
    current_test = "function with typed params";
    const auto result = transpile("fn add(a: i32, b: i32) -> i32 { return a + b; }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("auto add(std::int32_t a, std::int32_t b) noexcept -> std::int32_t {\n"));
    CHECK(result.output.contains("    return a + b;\n"));
}

static auto test_function_with_untyped_params() noexcept -> void {
    current_test = "function with untyped params";
    const auto result = transpile("fn add(a, b) { return a + b; }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("auto add(auto a, auto b) noexcept {\n"));
}

static auto test_function_without_return_type() noexcept -> void {
    current_test = "function without return type";
    const auto result = transpile("fn foo() {}");
    CHECK(!result.has_errors());
    CHECK(!result.output.contains(" -> void"));
}

static auto test_function_body_indentation() noexcept -> void {
    current_test = "function body indentation";
    const auto result = transpile("fn main() { let x = 1; let y = 2; return x + y; }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("    const auto x = 1;\n"));
    CHECK(result.output.contains("    const auto y = 2;\n"));
    CHECK(result.output.contains("    return x + y;\n"));
}

static auto test_nested_block_indentation() noexcept -> void {
    current_test = "nested block indentation";
    const auto result = transpile("fn main() { if true { return 1; } else { return 0; } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("    if (true) {\n        return 1;\n    } else {\n        return 0;\n    }\n"));
}

static auto test_while_loop_indentation() noexcept -> void {
    current_test = "while loop indentation";
    const auto result = transpile("fn main() { while true { let x = 1; } }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("    while (true) {\n        const auto x = 1;\n    }\n"));
}

static auto test_import() noexcept -> void {
    current_test = "import";
    const auto result = transpile("import std;");
    CHECK(!result.has_errors());
    CHECK(!result.output.contains("import std;"));
}

static auto test_enum() noexcept -> void {
    current_test = "enum";
    const auto result = transpile("enum Color { Red, Green, Blue }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("enum class Color {\n    Red,\n    Green,\n    Blue,\n};\n"));
}

static auto test_enum_with_size() noexcept -> void {
    current_test = "enum with explicit size";
    const auto result = transpile("enum Color : u8 { Red, Green, Blue }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("enum class Color : std::uint8_t {\n    Red,\n    Green,\n    Blue,\n};\n"));
}

static auto test_struct() noexcept -> void {
    current_test = "struct";
    const auto result = transpile("struct Point { x: i32, y: i32 }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("struct Point {\n    std::int32_t x;\n    std::int32_t y;\n};\n"));
}

static auto test_struct_field_types() noexcept -> void {
    current_test = "struct field types";
    const auto result = transpile("struct Data { name: char, count: u64 }");
    CHECK(!result.has_errors());
    CHECK(result.output.contains("    char name;\n"));
    CHECK(result.output.contains("    std::uint64_t count;\n"));
}

static auto test_emit_only_output() noexcept -> void {
    current_test = "emit only flag output";
    auto driver = Driver();
    driver.emit_only = true;
    driver.input_files.push_back("tests/test_helloworld.zero");
    CHECK_EQ(run(driver), 0);
}

auto main() noexcept -> int {
    test_empty_function();
    test_function_with_typed_params();
    test_function_with_untyped_params();
    test_function_without_return_type();
    test_function_body_indentation();
    test_nested_block_indentation();
    test_while_loop_indentation();
    test_import();
    test_enum();
    test_enum_with_size();
    test_struct();
    test_struct_field_types();
    test_emit_only_output();

    return print_test_result();
}
