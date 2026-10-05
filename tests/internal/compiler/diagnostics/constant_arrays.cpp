module carven:test.internal.compiler.diagnostics.constant_arrays;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Const arrays: owning elements require execution storage"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "owning elements do not acquire a recursively frozen source type",
             .source = R"(const fn make() -> [String; 1] => ["text"]; const value = make();)",
             .code = DiagnosticCode::ConstInitializer,
             .primary_text = "make()"},
            {.name = "nested owning elements cannot freeze into borrowed elements",
             .source =
                 R"(const fn make() -> [[String; 1]; 1] => [["text"]]; const value = make();)",
             .code = DiagnosticCode::ConstInitializer,
             .primary_text = "make()"},
        });
        check_compiler_errors(cases);
    };

    "Const arrays: freezing preserves ownership and ordinary backing lifetimes"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
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
                     "fn bad() { var local = values; let view = local.as_slice(); local[0] = 3; }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "local[0] = 3"},
            });
            check_compiler_errors(cases);
        };

    "Const indexing: unresolved names and wrong index types retain diagnostics"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
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
