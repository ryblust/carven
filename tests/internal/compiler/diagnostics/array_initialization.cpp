module carven:test.internal.compiler.diagnostics.array_initialization;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Array initialization: extent and element contracts reject invalid source"_test =
        [] static noexcept {
            constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "separate lambda expressions retain different nominal types",
                 .source = "fn bad() { let values = [[]() -> i32 => 1, []() -> i32 => 2]; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "[]() -> i32 => 2"},
                {.name = "zero lambda occurrences still check captured names",
                 .source = "fn bad() { let values = [[unknown]() -> i32 => 7; 0]; }",
                 .code = DiagnosticCode::LambdaCaptureInvalid,
                 .primary_text = "unknown"},
                {.name = "runtime extent",
                 .source = "fn bad(count: usize) { let values = [1; count]; }",
                 .code = DiagnosticCode::ConstArrayExtent,
                 .primary_text = "count"},
                {.name = "unbound static extent",
                 .source = "fn bad(const count: usize) { let values = [1; count]; }",
                 .code = DiagnosticCode::ConstArrayExtent,
                 .primary_text = "count"},
                {.name = "negative extent",
                 .source = "fn bad() { let values = [1; -1]; }",
                 .code = DiagnosticCode::ConstNegativeArrayExtent,
                 .primary_text = "-1"},
                {.name = "noninteger extent",
                 .source = "fn bad() { let values = [1; true]; }",
                 .code = DiagnosticCode::ConstArrayExtent,
                 .primary_text = "true"},
                {.name = "expected extent mismatch",
                 .source = "fn bad() { let values: [i32; 2] = [1; 3]; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "[1; 3]"},
                {.name = "zero count still checks names",
                 .source = "fn bad() { let values = [unknown; 0]; }",
                 .code = DiagnosticCode::NameUnresolved,
                 .primary_text = "unknown"},
                {.name = "zero count still checks element type",
                 .source = "fn bad() { let values: [i32; 0] = [true; 0]; }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "true"},
                {.name = "repeated Take requires an available owner each time",
                 .source = "fn bad() { let owner: String = \"text\"; let values = [&&owner; 2]; }",
                 .code = DiagnosticCode::AccessUnavailable,
                 .primary_text = "owner"},
                {.name = "implicit source expansion has a construction budget",
                 .source = "fn bad() { let values = [0; 0xffffffffffffffffu64]; }",
                 .code = DiagnosticCode::ConstLimit,
                 .primary_text = "[0; 0xffffffffffffffffu64]"},
                {.name = "zero count does not omit failure consumption checking",
                 .source = "enum Failure { Stop } const fn fail_value() -> i32 throw Failure { "
                           "throw Failure::Stop; } const values = [fail_value(); 0];",
                 .code = DiagnosticCode::EffectUnmarked,
                 .primary_text = "fail_value()"},
            });
            check_compiler_errors(cases);
        };

    "Array initialization: repeated unit results retain their value type"_test =
        [] static noexcept {
            check_compiler_accepts(
                "fn empty() {} fn accepted() { let none: [void; 0] = [empty(); 0]; "
                "let units: [void; 2] = [empty(); 2]; }"
            );
        };

    "Const arrays: freezing preserves ownership and ordinary backing lifetimes"_test =
        [] static noexcept {
            constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "whole-array Take leaves the source unavailable",
                 .source = "const fn bad() -> i32 { var values = [1, 2]; "
                           "let moved = &&values; return values[0]; } const result = bad();",
                 .code = DiagnosticCode::AccessUnavailable,
                 .primary_text = "values"},
                {.name = "a runtime copy of a constant cannot lend storage beyond its scope",
                 .source = "const values = [1, 2]; "
                           "fn bad() -> [i32] { let local = values; return local; }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "local"},
                {.name = "a view prevents mutating a runtime copy of a constant",
                 .source =
                     "const values = [1, 2]; "
                     "fn bad() { var local = values; let view = local.slice(); local[0] = 3; }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "local[0] = 3"},
            });
            check_compiler_errors(cases);
        };

    "Const indexing: unresolved names and wrong index types retain diagnostics"_test =
        [] static noexcept {
            constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "an unresolved index is diagnosed before constant call execution",
                 .source = "const fn data() -> [i32; 1] => [1]; const result = data()[unknown];",
                 .code = DiagnosticCode::NameUnresolved,
                 .primary_text = "unknown"},
                {.name = "index type checking follows a const function call",
                 .source = R"(const fn data() -> [i32; 1] => [1]; const result = data()["text"];)",
                 .code = DiagnosticCode::TypeIndexInteger,
                 .primary_text = R"("text")"},
            });
            check_compiler_errors(cases);
        };
});

} // namespace
