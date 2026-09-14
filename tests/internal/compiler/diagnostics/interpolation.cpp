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
        {.name = "known local is still runtime storage",
         .source = R"(fn bad() { let value = 42; const copy = value; })",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = "const copy = value"},
        {.name = "known local remains unavailable after Take",
         .source =
             R"(fn take(&&value: i32) {} fn bad() { let value = 42; take(&&value); let text = f"{value}"; })",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "value"},
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

TEST_CASE(
    "Compiler diagnostics: constant interpolation preserves access type and execution boundaries"
) {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "constant Write marker",
         .source = R"(const x = 1; const text = f"{&x}";)",
         .code = "CV-ACCESS-CALL-MISMATCH",
         .primary_text = "&x"},
        {.name = "constant Take marker",
         .source = R"(const x = 1; const text = f"{&&x}";)",
         .code = "CV-ACCESS-CALL-MISMATCH",
         .primary_text = "&&x"},
        {.name = "nested width access",
         .source = R"(const width = 4; const text = f"{42:0{&width}}";)",
         .code = "CV-ACCESS-CALL-MISMATCH",
         .primary_text = "&width"},
        {.name = "text factory access",
         .source = R"(const text = String::from_str(&"x");)",
         .code = "CV-ACCESS-CALL-MISMATCH",
         .primary_text = R"(&"x")"},
        {.name = "text factory argument",
         .source = R"(const text = String::from_str(42);)",
         .code = "CV-TYPE-MISMATCH",
         .primary_text = "42"},
        {.name = "unsupported specification",
         .source = R"(const text = f"{42:+04}";)",
         .code = "CV-CONST-EVALUATION",
         .primary_text = R"(f"{42:+04}")"},
        {.name = "dynamic width remains typed",
         .source = R"(const text = f"{42:0{"4"}}";)",
         .code = "CV-CONST-EVALUATION",
         .primary_text = R"(f"{42:0{"4"}}")"},
        {.name = "negative dynamic width",
         .source = R"(const text = f"{42:{-1}}";)",
         .code = "CV-CONST-EVALUATION",
         .primary_text = R"(f"{42:{-1}}")"},
        {.name = "runtime binding is not a required constant",
         .source = R"(fn bad() { let x = 1; const text = f"{x}"; })",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = R"(const text = f"{x}")"},
        {.name = "frozen result cannot retain String type",
         .source = R"(const text: String = f"x";)",
         .code = "CV-TYPE-MISMATCH",
         .primary_text = R"(f"x")"},
        {.name = "owning interpolation is not a canonical enum payload",
         .source = R"(enum E { Value(String) } const result = E::Value(f"x");)",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = R"(E::Value(f"x"))"},
        {.name = "owning text remains typed in nested calls",
         .source = R"(const fn consume(value: i32) -> i32 => value; const result = consume(f"x");)",
         .code = "CV-TYPE-MISMATCH",
         .primary_text = R"(f"x")"},
        {.name = "direct formatting byte budget",
         .source = R"(const text = f"x{1:1048576}";)",
         .code = "CV-CONST-LIMIT",
         .primary_text = R"(f"x{1:1048576}")"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: constant widths retain format grammar and resource limits") {
    const auto sources = std::to_array<std::string_view>({
        R"(const text = f"{7:2{2}}";)",
        R"(const text = f"{7:{2}{3}}";)",
        R"(const text = f"{7:{0}4}";)",
        R"(const text = f"{7:{2}0}";)",
        R"(const fn bad() -> String => f"{7:2{2}}"; const text = bad();)",
        R"(const fn bad() -> String => f"{7:{2}{3}}"; const text = bad();)",
        R"(const fn bad() -> String => f"{7:{0}4}"; const text = bad();)",
    });
    for (const auto source : sources) {
        const auto start = source.find("f\"");
        const auto finish = source.find("\";", start) + 1uz;
        const auto cases = std::array {CompilerErrorExpectation {
            .name = "invalid width structure",
            .source = source,
            .code = "CV-CONST-EVALUATION",
            .primary_text = source.substr(start, finish - start),
        }};
        check_compiler_errors(cases);
    }
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "direct field text budget",
         .source = R"(const text = f"{1:1048577}";)",
         .code = "CV-CONST-LIMIT",
         .primary_text = R"(f"{1:1048577}")"},
        {.name = "constant function field text budget",
         .source = R"(const fn large() -> String => f"{1:1048577}"; const text = large();)",
         .code = "CV-CONST-LIMIT",
         .primary_text = R"(f"{1:1048577}")"},
        {.name = "active void hole",
         .source = R"(const fn nothing() {} const text = f"{nothing()}";)",
         .code = "CV-TYPE-VALUE-REQUIRED",
         .primary_text = "nothing()"},
        {.name = "inactive void hole",
         .source = R"(const fn nothing() {} const text = false && f"{nothing()}".is_empty();)",
         .code = "CV-TYPE-VALUE-REQUIRED",
         .primary_text = "nothing()"},
    });
    check_compiler_errors(cases);
    const auto initializer = std::format("String::from_str(\"{}\")", std::string(1048577uz, 'x'));
    const auto source = std::format("const length = {}.len();", initializer);
    const auto large_text = std::array {CompilerErrorExpectation {
        .name = "direct text construction budget",
        .source = source,
        .code = "CV-CONST-LIMIT",
        .primary_text = initializer,
    }};
    check_compiler_errors(large_text);
}
