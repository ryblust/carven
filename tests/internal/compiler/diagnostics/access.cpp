module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.access;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: access failures preserve code and precise span") {
    static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
        {
            .name = "immutable assignment",
            .source = "fn invalid() { let value = 1; value = 2; }",
            .code = "CV-ACCESS-IMMUTABLE",
            .primary_text = "value",
        },
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: Take conversions preserve source access") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "String owner is unavailable after conversion",
         .source =
             "fn take(&&value: str) {} fn bad() { let text: String = \"hello\"; take(&&text); println(text); }",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "text"},
        {.name = "array owner is unavailable after conversion",
         .source =
             "fn take(&&value: [i32]) {} fn bad() { let values = [1, 2]; take(&&values); println(values[0]); }",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "values"},
        {.name = "Read source cannot be taken through conversion",
         .source = "fn take(&&value: str) {} fn bad(text: String) { take(&&text); }",
         .code = "CV-ACCESS-TAKE-OPERAND",
         .primary_text = "&&text"},
        {.name = "Write source cannot be taken through conversion",
         .source = "fn take(&&value: str) {} fn bad(&text: String) { take(&&text); }",
         .code = "CV-ACCESS-TAKE-OPERAND",
         .primary_text = "&&text"},
        {.name = "member cannot be taken through conversion",
         .source =
             "struct Box { text: String } fn take(&&value: str) {} fn bad() { let box = Box { text: \"hello\" }; take(&&box.text); }",
         .code = "CV-ACCESS-TAKE-OPERAND",
         .primary_text = "&&box.text"},
        {.name = "element cannot be taken through conversion",
         .source =
             "fn take(&&value: str) {} fn bad() { let texts: [String; 1] = [\"hello\"]; take(&&texts[0]); }",
         .code = "CV-ACCESS-TAKE-OPERAND",
         .primary_text = "&&texts[0]"},
        {.name = "native conversion consumes the Carven owner",
         .source =
             "import \"provider.hpp\"; fn take(&&value: i32) {} fn bad() { let value = ::make_value(); take(&&value); let copy = value; }",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "value"},
        {.name = "native conversion cannot consume a Read source",
         .source =
             "import \"provider.hpp\"; fn take(&&value: i32) {} fn bad(value: ::NativeValue) { take(&&value); }",
         .code = "CV-ACCESS-TAKE-OPERAND",
         .primary_text = "&&value"},
        {.name = "converted Take cannot escape through a returned view",
         .source =
             "fn relay(&&s: str) -> str { return s; } fn bad() { let text: String = \"hello\"; let view = relay(&&text); println(view); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "let view = relay(&&text)"},
        {.name = "converted Take conflicts with an earlier argument borrow",
         .source =
             "fn take(a: str, &&b: str) {} fn bad() { let s: String = \"hello\"; take(s, &&s); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "&&s"},
    });
    check_compiler_errors(cases);
}
