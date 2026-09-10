module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.interpolation;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: interpolation consumes Read operands and protects backing") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "Write marker",
         .source = R"(fn bad() { var x = 1; let s = f"{&x}"; })",
         .code = "CV-ACCESS-CALL-MISMATCH",
         .primary_text = "&x"},
        {.name = "Take marker",
         .source = R"(fn bad() { let x = 1; let s = f"{&&x}"; })",
         .code = "CV-ACCESS-CALL-MISMATCH",
         .primary_text = "&&x"},
        {.name = "constant",
         .source = R"(const text = f"plain";)",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = R"(f"plain")"},
        {.name = "pending view mutation",
         .source =
             R"(fn change(&s: String) -> i32 { s.clear(); return 1; } fn bad() { var s = String::new(); let r = f"{s.as_str()}{change(&s)}"; })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "dynamic width protects text",
         .source =
             R"(fn change(&s: String) -> i32 { s.clear(); return 1; } fn bad() { var s = String::new(); let r = f"{s.as_str():>{change(&s)}}"; })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "pending owner Take",
         .source =
             R"(fn consume(&&s: String) -> usize => s.len(); fn bad() { var s = String::new(); let r = f"{s}{consume(&&s)}"; })",
         .code = "CV-ACCESS-OPERATION-CONFLICT",
         .primary_text = "&&s"},
        {.name = "named holder remains after formatting",
         .source =
             R"(fn bad() { var s = String::new(); let v = s.as_str(); let r = f"{v}"; s.clear(); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "temporary result view cannot be saved",
         .source = R"(fn bad() { let v = f"plain".as_str(); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(let v = f"plain".as_str())"},
        {.name = "callable view boundary",
         .source = R"(fn bad(callback: fn() -> void) { let s = f"{callback}"; })",
         .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
         .primary_text = R"(f"{callback}")"},
        {.name = "Write capture boundary",
         .source =
             R"(fn bad() { var x = 1; let callback = [&x]() { x += 1; }; let s = f"{callback}"; })",
         .code = "CV-TYPE-CALLABLE-VIEW-ESCAPE",
         .primary_text = R"(f"{callback}")"},
    });
    check_compiler_errors(cases);
}
