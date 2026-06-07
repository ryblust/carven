#include "test_helpers.h"

import carven.common.source;
import carven.driver.pipeline;
import carven.driver.toolchain;
import carven.common.filesystem;
import carven.common.process;
import std;

static auto e2e_run(std::string_view source, std::string_view filepath) noexcept -> std::pair<int, std::string> {
    const auto driver = Driver{};
    const auto result = transpile(source, driver.language_standard, driver.import_std);
    if (!result.errors.empty() || result.output.empty()) return {-1, ""};

    const auto path = build_artifacts(filepath, driver.output_dir);
    ensure_directory(driver.output_dir);
    write_file_if_changed(path.cpp_path, result.output);

    const auto compile_args = build_compile_args(driver.compiler, path.cpp_path, path.exe_path, driver.language_standard);
    const auto [compile_code, _] = run_and_capture(compile_args);
    if (compile_code != 0) return { compile_code, "compile failed" };

    return run_and_capture(std::vector { path.exe_path });
}

static auto e2e_run_fixture(std::string_view name) noexcept -> std::pair<int, std::string> {
    auto source = SourceFile::from_file(name);
    if (!source.has_value()) return { -1, "cannot open fixture" };
    return e2e_run(source->text(), source->filepath());
}

static auto transpile_fixture(std::string_view name) noexcept -> bool {
    auto source = SourceFile::from_file(name);
    if (!source.has_value()) return false;
    const auto result = transpile(source->text(), 23, false);
    return result.errors.empty() && !result.output.empty();
}

TEST_CASE("E2E: helloworld") {
    const auto [code, stdout] = e2e_run_fixture("tests/fixtures/helloworld.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("Hello World"));
}

TEST_CASE("E2E: array") {
    CHECK(transpile_fixture("tests/fixtures/array.cv"));
}

TEST_CASE("E2E: match") {
    const auto [code, stdout] = e2e_run_fixture("tests/fixtures/match.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("two"));
    CHECK(stdout.contains("int"));
}

TEST_CASE("E2E: if expression lambda lowering") {
    static constexpr auto source = std::string_view {
        "import std;\n"
        "fn main() {\n"
        "    let x = 1 + if true { 2 } else { 3 };\n"
        "    std::println(\"{}\", x);\n"
        "}\n"
    };

    const auto [code, stdout] = e2e_run(source, "if_expr.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("3"));
}

TEST_CASE("E2E: multi-statement if expression branch") {
    static constexpr auto source = std::string_view {
        "import std;\n"
        "fn main() {\n"
        "    var n = 1;\n"
        "    let x = if true { n += 1; n } else { 0 };\n"
        "    std::println(\"{}\", x);\n"
        "}\n"
    };

    const auto [code, stdout] = e2e_run(source, "if_expr_multistmt.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("2"));
}

TEST_CASE("E2E: match expression evaluates value once") {
    static constexpr auto source = std::string_view {
        "import std;\n"
        "fn main() {\n"
        "    var n = 0;\n"
        "    let x = match ++n { 0 => { 0 } 1 => { n } _ => { 9 } };\n"
        "    std::println(\"{}\", x);\n"
        "}\n"
    };

    const auto [code, stdout] = e2e_run(source, "match_once.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("1"));
}

TEST_CASE("E2E: type match expression") {
    static constexpr auto source = std::string_view {
        "import std;\n"
        "fn classify(v) {\n"
        "    return match v { i32 => { 1 } _ => { 0 } };\n"
        "}\n"
        "fn main() {\n"
        "    std::println(\"{}\", classify(42));\n"
        "}\n"
    };

    const auto [code, stdout] = e2e_run(source, "type_match_expr.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("1"));
}

TEST_CASE("E2E: statement control flow return") {
    static constexpr auto source = std::string_view {
        "import std;\n"
        "fn from_if(x: i32) -> i32 {\n"
        "    if x == 1 { return 7; }\n"
        "    return 0;\n"
        "}\n"
        "fn from_match(x: i32) -> i32 {\n"
        "    match x { 1 => { return 8; } };\n"
        "    return 0;\n"
        "}\n"
        "fn main() {\n"
        "    std::println(\"{} {}\", from_if(1), from_match(1));\n"
        "}\n"
    };

    const auto [code, stdout] = e2e_run(source, "statement_return.cv");
    CHECK_EQ(code, 0);
    CHECK(stdout.contains("7 8"));
}

TEST_CASE("E2E: syntax error") {
    auto source = SourceFile::from_file("tests/fixtures/error.cv");
    REQUIRE(source.has_value());
    const auto result = transpile(source->text(), 23, false);
    CHECK(!result.errors.empty());
}
