module carven:test.internal.compiler.diagnostics.access;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Compiler diagnostics: access failures preserve code and precise span",
        [] static noexcept {
            static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {
                    .name = "check message may consume an owner before continuing",
                    .source =
                        "fn bad(condition: bool) { var text: String = \"owned\"; "
                        "check(condition, if true { &&text } else { &&text }); println(text); }",
                    .code = DiagnosticCode::AccessUnavailable,
                    .primary_text = "text",
                },
                {
                    .name = "immutable assignment",
                    .source = "fn invalid() { let value = 1; value = 2; }",
                    .code = DiagnosticCode::AccessImmutable,
                    .primary_text = "value",
                },
            });
            check_compiler_errors(cases);
        }
    );

    ct::test("Compiler diagnostics: Take conversions preserve source access", [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "String owner is unavailable after conversion",
             .source =
                 "fn take(&&value: str) {} fn bad() { let text: String = \"hello\"; take(&&text); println(text); }",
             .code = DiagnosticCode::AccessUnavailable,
             .primary_text = "text"},
            {.name = "array owner is unavailable after conversion",
             .source =
                 "fn take(&&value: [i32]) {} fn bad() { let values = [1, 2]; take(&&values); println(values[0]); }",
             .code = DiagnosticCode::AccessUnavailable,
             .primary_text = "values"},
            {.name = "Read source cannot be taken through conversion",
             .source = "fn take(&&value: str) {} fn bad(text: String) { take(&&text); }",
             .code = DiagnosticCode::AccessTakeOperand,
             .primary_text = "&&text"},
            {.name = "Write source cannot be taken through conversion",
             .source = "fn take(&&value: str) {} fn bad(&text: String) { take(&&text); }",
             .code = DiagnosticCode::AccessTakeOperand,
             .primary_text = "&&text"},
            {.name = "member cannot be taken through conversion",
             .source =
                 "struct Box { text: String } fn take(&&value: str) {} fn bad() { let box = Box { text: \"hello\" }; take(&&box.text); }",
             .code = DiagnosticCode::AccessTakeOperand,
             .primary_text = "&&box.text"},
            {.name = "element cannot be taken through conversion",
             .source =
                 "fn take(&&value: str) {} fn bad() { let texts: [String; 1] = [\"hello\"]; take(&&texts[0]); }",
             .code = DiagnosticCode::AccessTakeOperand,
             .primary_text = "&&texts[0]"},
            {.name = "native conversion consumes the Carven owner",
             .source =
                 "import \"provider.hpp\"; fn take(&&value: i32) {} fn bad() { let value = ::make_value(); take(&&value); let copy = value; }",
             .code = DiagnosticCode::AccessUnavailable,
             .primary_text = "value"},
            {.name = "native conversion cannot consume a Read source",
             .source =
                 "import \"provider.hpp\"; fn take(&&value: i32) {} fn bad(value: ::NativeValue) { take(&&value); }",
             .code = DiagnosticCode::AccessTakeOperand,
             .primary_text = "&&value"},
            {.name = "converted Take cannot escape through a returned view",
             .source =
                 "fn relay(&&s: str) -> str { return s; } fn bad() { let text: String = \"hello\"; let view = relay(&&text); println(view); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "let view = relay(&&text)"},
            {.name = "converted Take conflicts with an earlier argument borrow",
             .source =
                 "fn take(a: str, &&b: str) {} fn bad() { let s: String = \"hello\"; take(s, &&s); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "&&s"},
        });
        check_compiler_errors(cases);
    });
});

} // namespace
