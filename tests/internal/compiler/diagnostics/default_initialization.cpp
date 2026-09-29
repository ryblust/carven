module carven:test.internal.compiler.diagnostics.default_initialization;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Defaults: type availability and construction completeness", [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "numeric enum has no implicit selected case",
             .source = "enum Choice { Item } fn f() { let value = Choice {}; }",
             .code = DiagnosticCode::TypeDefaultInitialization,
             .primary_text = "Choice {}"},
            {.name = "nested enum remains required",
             .source =
                 "enum Choice { Item } struct Inner { choice: Choice } struct Outer { inner: Inner } fn f() { let value = Outer {}; }",
             .code = DiagnosticCode::TypeDefaultInitialization,
             .primary_text = "Outer {}"},
            {.name = "callable field needs a target",
             .source = "struct Holder { action: fn() -> void } fn f() { let value = Holder {}; }",
             .code = DiagnosticCode::TypeDefaultInitialization,
             .primary_text = "Holder {}"},
            {.name = "nonempty array needs defaultable elements",
             .source =
                 "enum Choice { Item } struct Holder { values: [Choice; 2] } fn f() { let value = Holder {}; }",
             .code = DiagnosticCode::TypeDefaultInitialization,
             .primary_text = "Holder {}"},
            {.name = "positional construction requires the declared field count",
             .source = "struct Holder { number: i32 } fn f() { let value = Holder { 1, 2 }; }",
             .code = DiagnosticCode::TypeConstructArity,
             .primary_text = "1, 2"},
            {.name = "constant named construction cannot omit defaultable fields",
             .source = "struct Pair { first: i32, second: i32 } const value = Pair { second: 1 };",
             .code = DiagnosticCode::TypeConstructArity,
             .primary_text = "second: 1"},
            {.name = "default pointer does not prove nonnull",
             .source =
                 "struct Holder { pointer: ptr<i32> } fn f() { let value = Holder {}; println(*value.pointer); }",
             .code = DiagnosticCode::PointerNonNull,
             .primary_text = "*value.pointer"},
            {.name = "implicit aggregate construction obeys execution budgets",
             .source = "struct Huge { values: [i32; 65537] } const value = Huge {};",
             .code = DiagnosticCode::ConstLimit,
             .primary_text = "Huge {}"},
        });
        check_compiler_errors(cases);
    });
});

} // namespace
