module carven:test.internal.compiler.diagnostics.classes;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: class privacy reports the selected source member"_test = [] static noexcept {
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
            {.name = "const operations retain private access",
             .source =
                 "class C { private const fn secret() -> i32 => 1; } const fn read() -> i32 => C::secret();",
             .code = DiagnosticCode::AccessClassPrivate,
             .primary_text = "secret"},
            {.name = "const operations require const callees",
             .source =
                 "class C { fn read(self) -> i32 => 1; const fn use(self) -> i32 => self.read(); }",
             .code = DiagnosticCode::ConstAdmission,
             .primary_text = "self.read()"},
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
    };
});

} // namespace
