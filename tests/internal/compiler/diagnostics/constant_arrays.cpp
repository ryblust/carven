module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.constant_arrays;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Const arrays: owning elements require execution storage") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "owning elements do not acquire a recursively frozen source type",
         .source = R"(const fn make() -> [String; 1] => ["text"]; const value = make();)",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = "make()"},
        {.name = "nested owning elements cannot freeze into borrowed elements",
         .source = R"(const fn make() -> [[String; 1]; 1] => [["text"]]; const value = make();)",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = "make()"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Const arrays: freezing preserves ownership and ordinary backing lifetimes") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "whole-array Take leaves the source unavailable",
         .source = "const fn bad() -> i32 { var values = [1, 2]; "
                   "let moved = &&values; return values[0]; } const result = bad();",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "values"},
        {.name = "a runtime copy of a constant cannot lend storage beyond its scope",
         .source = "const values = [1, 2]; "
                   "fn bad() -> [i32] { let local = values; return local; }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "local"},
        {.name = "a view prevents mutating a runtime copy of a constant",
         .source = "const values = [1, 2]; "
                   "fn bad() { var local = values; let view = local.as_slice(); local[0] = 3; }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "local[0] = 3"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Const indexing preserves name and index type diagnostics") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "an unresolved index is diagnosed before constant call execution",
         .source = "const fn data() -> [i32; 1] => [1]; const result = data()[unknown];",
         .code = "CV-NAME-UNRESOLVED",
         .primary_text = "unknown"},
        {.name = "index type checking follows a const function call",
         .source = R"(const fn data() -> [i32; 1] => [1]; const result = data()["text"];)",
         .code = "CV-TYPE-INDEX-INTEGER",
         .primary_text = R"("text")"},
    });
    check_compiler_errors(cases);
}
