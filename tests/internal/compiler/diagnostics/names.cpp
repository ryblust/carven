module carven:test.internal.compiler.diagnostics.names;

import :artifacts;
import :backend.generation.request;
import :compiler.compile;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: names failures preserve code and precise span"_test =
        [] static noexcept {
            static constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {
                    .name = "unresolved value",
                    .source = "fn invalid() { missing(); }",
                    .code = DiagnosticCode::NameUnresolved,
                    .primary_text = "missing",
                },
                {
                    .name = "binding is not visible in its own initializer",
                    .source = "fn invalid() { let value: i32 = value; }",
                    .code = DiagnosticCode::NameUnresolved,
                    .primary_text = "value",
                },
                {
                    .name = "local name is unavailable after its frame ends",
                    .source = "fn invalid() { if true { let inner = 1; } let result = inner; }",
                    .code = DiagnosticCode::NameUnresolved,
                    .primary_text = "inner",
                },
                {
                    .name = "runtime names require an explicit capture",
                    .source =
                        "fn invalid() { let local = 1; let callback = []() { return local; }; }",
                    .code = DiagnosticCode::NameUnresolved,
                    .primary_text = "local",
                },
                {
                    .name = "duplicate parameter",
                    .source = "fn invalid(value: i32, value: i32) {}",
                    .code = DiagnosticCode::NameDuplicateParameter,
                    .primary_text = "value",
                },
                {
                    .name = "duplicate C++ import parameter",
                    .source = "private import(cpp) fn invalid(value: i32, value: i32);",
                    .code = DiagnosticCode::NameDuplicateParameter,
                    .primary_text = "value",
                },
            });
            check_compiler_errors(cases);
        };

    "Compiler diagnostics: core type constructor names cannot declare module symbols"_test =
        [] static noexcept {
            constexpr auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "pointer constructor cannot name a structure",
                 .source = "struct ptr {}",
                 .code = DiagnosticCode::Catalog,
                 .primary_text = "ptr"},
                {.name = "range constructor cannot name an enum",
                 .source = "enum range { Value }",
                 .code = DiagnosticCode::Catalog,
                 .primary_text = "range"},
                {.name = "pointer constructor cannot name a function",
                 .source = "fn ptr() {}",
                 .code = DiagnosticCode::Catalog,
                 .primary_text = "ptr"},
                {.name = "range constructor cannot name a constant",
                 .source = "const range = 7;",
                 .code = DiagnosticCode::Catalog,
                 .primary_text = "range"},
            });
            check_compiler_errors(cases);
        };
});

} // namespace
