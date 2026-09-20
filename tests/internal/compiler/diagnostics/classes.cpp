module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.classes;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: class privacy reports the selected source member") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "private field",
         .source = "class C { value: i32, } fn read(c: C) -> i32 { return c.value; }",
         .code = "CV-ACCESS-CLASS-PRIVATE",
         .primary_text = "value"},
        {.name = "private operation",
         .source =
             "class C { private fn secret(self) -> i32 { return 1; } } fn read(c: C) -> i32 { return c.secret(); }",
         .code = "CV-ACCESS-CLASS-PRIVATE",
         .primary_text = "secret"},
        {.name = "class equality",
         .source =
             "class C { value: i32, fn create() -> C { return { value: 1 }; } } fn equal() -> bool { return C::create() == C::create(); }",
         .code = "CV-TYPE-EQUALITY-UNSUPPORTED",
         .primary_text = "=="},
        {.name = "containing record equality",
         .source =
             "class C { value: i32, fn create() -> C { return { value: 1 }; } } struct Box { value: C } fn equal() -> bool { return Box { C::create() } == Box { C::create() }; }",
         .code = "CV-TYPE-EQUALITY-UNSUPPORTED",
         .primary_text = "=="},
    });
    check_compiler_errors(cases);
}
