module carven:test.internal.compiler.diagnostics.contextual_construction;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Compiler diagnostics: contextual construction requires a known admissible destination",
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "no destination",
                 .source = "let value = { field: 1 };",
                 .code = DiagnosticCode::TypeConstructContext,
                 .primary_text = "{ field: 1 }"},
                {.name = "no backwards inference",
                 .source = "struct S { field: i32 } fn f() => { field: 1 };",
                 .code = DiagnosticCode::TypeConstructContext,
                 .primary_text = "{ field: 1 }"},
                {.name = "no failure type selection",
                 .source = "struct E { field: i32 } fn f() throw E { throw { field: 1 }; }",
                 .code = DiagnosticCode::TypeConstructContext,
                 .primary_text = "{ field: 1 }"},
                {.name = "native construction stays explicit",
                 .source = "fn f() -> ::Native { return {}; }",
                 .code = DiagnosticCode::TypeConstructContext,
                 .primary_text = "{}"},
                {.name = "private representation",
                 .source = "class C { value: i32 } fn f() -> C { return { value: 1 }; }",
                 .code = DiagnosticCode::AccessClassPrivate,
                 .primary_text = "{ value: 1 }"},
                {.name = "class has no default",
                 .source = "class C { value: i32, fn f() -> C { return {}; } }",
                 .code = DiagnosticCode::TypeDefaultInitialization,
                 .primary_text = "{}"},
                {.name = "field type is checked",
                 .source = "struct S { value: i32 } fn f() -> S { return { value: true }; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "true"},
            });
            check_compiler_errors(cases);
        }
    );
});

} // namespace
