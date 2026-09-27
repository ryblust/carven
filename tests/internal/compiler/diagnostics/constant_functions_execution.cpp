module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.constant_functions_execution;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Const functions: execution failures identify the operation inside the called body") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
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

TEST_CASE("Const functions: definitions obey source ownership contracts without execution") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "view of a local owner cannot escape",
         .source =
             R"(const fn escape() -> str { let text: String = "local"; return text.as_str(); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.as_str()"},
        {.name = "a named view keeps the original owner borrowed",
         .source =
             R"(const fn mutate() -> usize { var text: String = "local"; let view = text.as_str(); text.clear(); return view.len(); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.clear()"},
        {.name = "taking a String leaves the source unavailable",
         .source =
             R"(const fn moved() -> usize { var text: String = "local"; let owner = &&text; return text.len(); })",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "text.len()"},
        {.name = "a pending view keeps later interpolation operands from mutating backing",
         .source =
             R"(const fn mutate() -> String { var text: String = "local"; return f"{text.as_str()}:{if true { text.clear(); 1 } else { 0 }}"; })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.clear()"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Const functions: required execution cannot observe or publish expired text views") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "returned text cannot outlive its local owner",
         .source = R"(
             const fn escape() -> str {
                 let text: String = "local";
                 return text.as_str();
             }
             const result = escape();
         )",
         .code = "CV-CONST-EVALUATION",
         .primary_text = "escape()"},
        {.name = "aggregate publication checks borrowed text fields",
         .source = R"(
             struct Entry { value: str }
             const fn escape() -> Entry {
                 let text: String = "local";
                 return Entry { text.as_str() };
             }
             const result = escape();
         )",
         .code = "CV-CONST-EVALUATION",
         .primary_text = "escape()"},
        {.name = "constant block cannot print invalidated text",
         .source = R"(const {
             var text: String = "local";
             let view = text.as_str();
             text.clear();
             println(view);
         })",
         .code = "CV-CONST-EVALUATION",
         .primary_text = "view"},
        {.name = "text equality rejects backing invalidated by its right operand",
         .source = R"(
             const fn reset(address: ptr<&String>) -> str {
                 if address != nullptr { (*address).clear(); }
                 return "old";
             }
             const fn compare() -> bool {
                 var text: String = "old";
                 let address = addressof(&text);
                 return text.as_str() == reset(address);
             }
             const result = compare();
         )",
         .code = "CV-CONST-EVALUATION",
         .primary_text = "=="},
        {.name = "nested aggregate equality rejects invalidated text fields",
         .source = R"(
             struct Entry { value: str }
             struct Group { entry: Entry }
             const fn reset(address: ptr<&String>) -> Group {
                 if address != nullptr { (*address).clear(); }
                 return Group { Entry { "old" } };
             }
             const fn compare() -> bool {
                 var text: String = "old";
                 let address = addressof(&text);
                 return Group { Entry { text.as_str() } } != reset(address);
             }
             const result = compare();
         )",
         .code = "CV-CONST-EVALUATION",
         .primary_text = "!="},
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
    });
    check_compiler_errors(cases);
}
