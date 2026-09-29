module carven:test.internal.compiler.diagnostics.classes;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Compiler diagnostics: class privacy reports the selected source member", [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "private field",
             .source = "class C { value: i32, } fn read(c: C) -> i32 { return c.value; }",
             .code = DiagnosticCode::AccessClassPrivate,
             .primary_text = "value"},
            {.name = "private operation",
             .source =
                 "class C { private fn secret(self) -> i32 { return 1; } } fn read(c: C) -> i32 { return c.secret(); }",
             .code = DiagnosticCode::AccessClassPrivate,
             .primary_text = "secret"},
            {.name = "class equality",
             .source =
                 "class C { value: i32, fn create() -> C { return { value: 1 }; } } fn equal() -> bool { return C::create() == C::create(); }",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "=="},
            {.name = "containing record equality",
             .source =
                 "class C { value: i32, fn create() -> C { return { value: 1 }; } } struct Box { value: C } fn equal() -> bool { return Box { C::create() } == Box { C::create() }; }",
             .code = DiagnosticCode::TypeEqualityUnsupported,
             .primary_text = "=="},
        });
        check_compiler_errors(cases);
    });
});

} // namespace
