module carven:test.internal.semantic.analysis.standard_modules;

import :diagnostics.code;
import :diagnostics.diagnostic;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {
const TestSuite suite([] static noexcept {
    "Standard modules: explicit defaults record usage and native selections can shadow defaults"_test =
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(R"(
import std::core using assert as verify;
import std::io using print as write;
import std::testing using check as ensure;
fn emit() { let output: fn(str) -> void = write; output("value"); }
test "aliases" { let check_value: fn(bool) -> void = ensure; check_value(true); verify(true); }
const test "direct aliases" { ensure(true); verify(true); }
)");
            expect_no_diagnostic(diagnostics, DiagnosticCode::LintUnusedImport);
            for (const auto& item : diagnostics) {
                expect(item.finding.severity != DiagnosticSeverity::Error);
            }
            static_cast<void>(
                analyze_test_program("import <native> using vendor::print; fn run() { print(7); }")
            );
        };

    "Standard modules: async aliases retain operation types and call contracts"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
import std::async using { Operation as Pending, when_all as join, yield_once as pause, wait_ready as ready, close as finish };
const async fn one() -> i32 { await pause(); return 1; }
fn pending() -> Pending<i32> => one();
const test "async aliases" {
    check(await join(one(), one()) == (1, 1));
    async let child = one();
    let _ = await ready(child);
    await finish(child);
}
)"));
        };
    "Standard modules: selected wrong kinds never fall back and specialized names require imports"_test =
        [] static noexcept {
            const auto cases = std::to_array<std::pair<std::string_view, DiagnosticCode>>({
                {"fn wrong<String>() { let x = String::from_str(\"hello\"); } fn use() { wrong<i32>(); }",
                 DiagnosticCode::TypeGenericDefinition},
                {"fn run(value: u8x16) {}", DiagnosticCode::TypeUnresolved},
                {"fn run(value: Operation<i32>) {}", DiagnosticCode::TypeUnresolved},
                {"async fn run() { await yield_once(); }", DiagnosticCode::NameUnresolved},
                {"fn String() {} fn run(value: String) {}", DiagnosticCode::TypeUnresolved},
                {"fn run(String: i32) { String::from_str(\"value\"); }",
                 DiagnosticCode::TypeMismatch},
                {"import std::io using print as emit; const emit = 1;",
                 DiagnosticCode::ImportResolution},
                {"import std::io using { print as emit, print as emit };",
                 DiagnosticCode::ImportResolution},
                {"import std::core using String as ptr;", DiagnosticCode::ImportResolution},
            });
            for (const auto& [source, code] : cases) {
                expect_diagnostic(analyze_test_errors(std::string(source)), code)
                    .note("source = ", source);
            }
        };
    "Standard modules: timeout dependencies are lazy and defaults are weak"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
import std::time using timeout as limit;
import std::core using String as Text;
fn run(String: i32) -> String { let _ = String; return Text::from_str("ok"); }
fn shadow(limit: fn() -> i32) -> i32 => limit();
fn print(value: i32) -> i32 => value;
const result = 7i32;
)"));
            expect_diagnostic(
                analyze_test_errors(R"(
import std::time using timeout as limit;
async fn one() -> i32 => 1;
async fn run() { await limit(one(), 1); }
)"),
                DiagnosticCode::TypeUnresolved
            );
        };
});
}
