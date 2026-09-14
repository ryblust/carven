module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.constant_arrays;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Const arrays: unsupported owning elements and views are diagnosed at definition") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "owning elements do not acquire a recursively frozen source type",
         .source = R"(const fn make() -> [String; 1] => ["text"];)",
         .code = "CV-CONST-ADMISSION",
         .primary_text = R"(const fn make() -> [String; 1] => ["text"];)"},
        {.name = "nested owning elements are outside the admitted subset",
         .source = R"(const fn make() -> [[String; 1]; 1] => [["text"]];)",
         .code = "CV-CONST-ADMISSION",
         .primary_text = R"(const fn make() -> [[String; 1]; 1] => [["text"]];)"},
        {.name = "array views are not const operations",
         .source = "const fn size(values: [i32; 2]) -> usize => values.as_slice().len();",
         .code = "CV-CONST-ADMISSION",
         .primary_text = "values.as_slice().len()"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Const arrays: freezing preserves ownership and ordinary backing lifetimes") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "whole-array Take leaves the source unavailable",
         .source = "const fn bad() -> i32 { var values = [1, 2]; "
                   "let moved = &&values; return values[0]; } const result = bad();",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "values[0]"},
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

TEST_CASE("Const indexing: independent name errors precede contextual admission errors") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "an unresolved index is diagnosed even when the receiver is not constant",
         .source = "fn data() -> [i32; 1] => [1]; const result = data()[unknown];",
         .code = "CV-NAME-UNRESOLVED",
         .primary_text = "unknown"},
        {.name = "index type checking requires an admitted receiver",
         .source = R"(fn data() -> [i32; 1] => [1]; const result = data()["text"];)",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = R"(data()["text"])"},
    });
    check_compiler_errors(cases);
}
