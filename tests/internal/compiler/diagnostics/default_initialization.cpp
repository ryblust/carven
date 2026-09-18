module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.default_initialization;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Defaults: unavailable values require explicit initialization") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "numeric enum has no implicit selected case",
         .source = "enum Choice { Item } fn f() { let value = Choice {}; }",
         .code = "CV-TYPE-DEFAULT-INITIALIZATION",
         .primary_text = "Choice {}"},
        {.name = "nested enum remains required",
         .source =
             "enum Choice { Item } struct Inner { choice: Choice } struct Outer { inner: Inner } fn f() { let value = Outer {}; }",
         .code = "CV-TYPE-DEFAULT-INITIALIZATION",
         .primary_text = "Outer {}"},
        {.name = "callable field needs a target",
         .source = "struct Holder { action: fn() -> void } fn f() { let value = Holder {}; }",
         .code = "CV-TYPE-DEFAULT-INITIALIZATION",
         .primary_text = "Holder {}"},
        {.name = "nonempty array needs defaultable elements",
         .source =
             "enum Choice { Item } struct Holder { values: [Choice; 2] } fn f() { let value = Holder {}; }",
         .code = "CV-TYPE-DEFAULT-INITIALIZATION",
         .primary_text = "Holder {}"},
        {.name = "extra positional values remain invalid",
         .source = "struct Holder { number: i32 } fn f() { let value = Holder { 1, 2 }; }",
         .code = "CV-TYPE-CONSTRUCT-ARITY",
         .primary_text = "1, 2"},
        {.name = "default pointer does not prove nonnull",
         .source =
             "struct Holder { pointer: ptr<i32> } fn f() { let value = Holder {}; println(*value.pointer); }",
         .code = "CV-PTR-NONNULL",
         .primary_text = "*value.pointer"},
        {.name = "implicit aggregate construction obeys execution budgets",
         .source = "struct Huge { values: [i32; 65537] } const value = Huge {};",
         .code = "CV-CONST-LIMIT",
         .primary_text = "Huge {}"},
    });
    check_compiler_errors(cases);
}
