module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.contextual_construction;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: contextual construction requires a known admissible destination") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "no destination",
         .source = "let value = { field: 1 };",
         .code = "CV-TYPE-CONSTRUCT-CONTEXT",
         .primary_text = "{ field: 1 }"},
        {.name = "no backwards inference",
         .source = "struct S { field: i32 } fn f() => { field: 1 };",
         .code = "CV-TYPE-CONSTRUCT-CONTEXT",
         .primary_text = "{ field: 1 }"},
        {.name = "no failure type selection",
         .source = "struct E { field: i32 } fn f() throw E { throw { field: 1 }; }",
         .code = "CV-TYPE-CONSTRUCT-CONTEXT",
         .primary_text = "{ field: 1 }"},
        {.name = "native construction stays explicit",
         .source = "fn f() -> ::Native { return {}; }",
         .code = "CV-TYPE-CONSTRUCT-CONTEXT",
         .primary_text = "{}"},
        {.name = "private representation",
         .source = "class C { value: i32 } fn f() -> C { return { value: 1 }; }",
         .code = "CV-ACCESS-CLASS-PRIVATE",
         .primary_text = "{ value: 1 }"},
        {.name = "class has no default",
         .source = "class C { value: i32, fn f() -> C { return {}; } }",
         .code = "CV-TYPE-DEFAULT-INITIALIZATION",
         .primary_text = "{}"},
        {.name = "field type is checked",
         .source = "struct S { value: i32 } fn f() -> S { return { value: true }; }",
         .code = "CV-TYPE-MISMATCH",
         .primary_text = "true"},
    });
    check_compiler_errors(cases);
}
