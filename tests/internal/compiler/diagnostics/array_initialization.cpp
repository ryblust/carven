module carven:test.internal.compiler.diagnostics.array_initialization;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Array initialization: extent and element contracts reject invalid source"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
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
                {.name = "zero count requires an element value",
                 .source = "fn empty() {} fn bad() { let values = [empty(); 0]; }",
                 .code = DiagnosticCode::TypeValueRequired,
                 .primary_text = "empty()"},
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
});

} // namespace
