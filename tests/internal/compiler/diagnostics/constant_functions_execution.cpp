module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.constant_functions_execution;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Const functions: execution failures identify the operation inside the called body") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "runtime unsigned arithmetic cannot wrap during constant execution",
         .source = "const fn increment(value: u8) -> u8 => value + 1u8; "
                   "const result = increment(255u8);",
         .code = "CV-CONST-OVERFLOW",
         .primary_text = "+"},
        {.name = "division by a value supplied at the constant call",
         .source = "const fn divide(value: i32) -> i32 => 12 / value; "
                   "const result = divide(0);",
         .code = "CV-CONST-DIVIDE-BY-ZERO",
         .primary_text = "/"},
        {.name = "unsupported formatting remains an execution error",
         .source =
             R"(const fn format(value: i32) -> String => f"{value:+}"; const result = format(7);)",
         .code = "CV-CONST-EVALUATION",
         .primary_text = R"(f"{value:+}")"},
        {.name = "text cannot masquerade as a dynamic integer width",
         .source =
             R"(const fn format(width: str) -> String => f"{7:0{width}}"; const result = format("4");)",
         .code = "CV-CONST-EVALUATION",
         .primary_text = R"(f"{7:0{width}}")"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Const functions: freezing results does not bypass source ownership validation") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "view of a local owner cannot escape",
         .source =
             R"(const fn escape() -> str { let text: String = "local"; return text.as_str(); } const result = escape();)",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.as_str()"},
        {.name = "a named view keeps the original owner borrowed",
         .source =
             R"(const fn mutate() -> usize { var text: String = "local"; let view = text.as_str(); text.clear(); return view.len(); } const result = mutate();)",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.clear()"},
        {.name = "taking a String leaves the source unavailable",
         .source =
             R"(const fn moved() -> usize { var text: String = "local"; let owner = &&text; return text.len(); } const result = moved();)",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "text.len()"},
        {.name = "a pending view keeps later interpolation operands from mutating backing",
         .source =
             R"(const fn mutate() -> String { var text: String = "local"; return f"{text.as_str()}:{if true { text.clear(); 1 } else { 0 }}"; } const result = mutate();)",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.clear()"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Const functions: typed intermediate values cannot corrupt constant publication") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "owning literal cannot become a borrowed enum constant payload",
         .source = R"(enum E { Value(String) } const result = E::Value("x");)",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = R"(E::Value("x"))"},
        {.name = "owning call result cannot become a borrowed enum constant payload",
         .source =
             R"(const fn make() -> String => "x"; enum E { Value(String) } const result = E::Value(make());)",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = "E::Value(make())"},
        {.name = "skipped pointer narrowing preserves a deferred value until admission rejects it",
         .source = "const fn make() -> ptr<&i32> => nullptr; "
                   "const fn observe(value: ptr<i32>) -> bool => false; "
                   "const result = false && observe(make());",
         .code = "CV-CONST-ADMISSION",
         .primary_text = "const fn make() -> ptr<&i32> => nullptr;"},
    });
    check_compiler_errors(cases);
}
