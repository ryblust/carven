module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.names;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :compiler.request;
import :diagnostics.diagnostic;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.compiler.diagnostics.fixture;
import std;

using compiler_diagnostics_test::ErrorExpectation;
using compiler_diagnostics_test::check_errors;
using compiler_diagnostics_test::find_diagnostic;

TEST_CASE("Compiler diagnostics: names failures preserve code and precise span") {
    static constexpr auto cases = std::to_array<ErrorExpectation>({
        {
            .name = "unresolved value",
            .source = "fn invalid() { missing(); }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "missing",
        },
        {
            .name = "binding is not visible in its own initializer",
            .source = "fn invalid() { let value: i32 = value; }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "value",
        },
        {
            .name = "local name is unavailable after its frame ends",
            .source = "fn invalid() { if true { let inner = 1; } let result = inner; }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "inner",
        },
        {
            .name = "runtime names require an explicit capture",
            .source = "fn invalid() { let local = 1; let callback = []() { return local; }; }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "local",
        },
        {
            .name = "duplicate parameter",
            .source = "fn invalid(value: i32, value: i32) {}",
            .code = "CV-NAME-DUPLICATE-PARAMETER",
            .primary_text = "value",
        },
        {
            .name = "duplicate C++ import parameter",
            .source = "private import(cpp) fn invalid(value: i32, value: i32);",
            .code = "CV-NAME-DUPLICATE-PARAMETER",
            .primary_text = "value",
        },
        {
            .name = "production has no implicit check name",
            .source = "fn invalid() { check(true); }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "check",
        },
        {
            .name = "lambda clears inline-test context",
            .source = "test \"invalid\" { let callback = []() { check(true); }; }",
            .code = "CV-NAME-UNRESOLVED",
            .primary_text = "check",
        },
    });
    check_errors(cases);
}
