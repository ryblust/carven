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
import std::simd using { f32x4 as Lanes, mask4 as Flags }
struct f32x4 { value: i32 }
fn emit() { let output: fn(str) -> void = write; output("value"); }
fn lanes(value: Lanes) -> Flags => value.is_finite();
fn local(value: f32x4) -> i32 => value.value;
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
import std::async using { Operation as Pending, when_all as join, yield_once as pause, wait_ready as ready, close as finish }
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
            constexpr auto cases = std::to_array<std::pair<std::string_view, DiagnosticCode>>({
                {"fn wrong<String>() { let x = String::from_str(\"hello\"); } fn use() { wrong<i32>(); }",
                 DiagnosticCode::TypeGenericDefinition},
                {"fn run(value: Operation<i32>) {}", DiagnosticCode::TypeUnresolved},
                {"async fn run() { await yield_once(); }", DiagnosticCode::NameUnresolved},
                {"fn String() {} fn run(value: String) {}", DiagnosticCode::TypeUnresolved},
                {"fn run(String: i32) { String::from_str(\"value\"); }",
                 DiagnosticCode::TypeMismatch},
                {"import std::io using print as emit; const emit = 1;",
                 DiagnosticCode::ImportResolution},
                {"import std::io using { print as emit, print as emit }",
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
            constexpr auto standard_modules = std::to_array<std::string_view>({"time"});
            static_cast<void>(analyze_test_program(R"(
import std::time using timeout as limit;
import std::core using String as Text;
fn run(String: i32) -> String { let _ = String; return Text::from_str("ok"); }
fn shadow(limit: fn() -> i32) -> i32 => limit();
fn print(value: i32) -> i32 => value;
const result = 7i32;
)"));
            expect_diagnostic(
                analyze_test_errors(
                    R"(
import std::time using timeout as limit;
async fn one() -> i32 => 1;
async fn run() { await limit(one(), 1); }
)",
                    standard_modules
                ),
                DiagnosticCode::TypeMismatch
            );
        };

    "Standard TOML: collection views retain their owner through nested traversal"_test =
        [] static noexcept {
            constexpr auto modules = std::to_array<std::string_view>({
                "core",
                "toml",
                "toml.node",
                "toml.key",
                "toml.scalar",
                "toml.string",
                "toml.syntax",
                "utf",
                "utf.scan",
                "utf.block",
                "simd.bytes",
                "simd.scan",
            });
            constexpr auto imports = std::string_view(
                "import std::toml using { TOMLValue, TOMLError, TOMLValueError }\n"
            );
            static_cast<void>(analyze_test_program(
                std::string(imports) + R"(
fn nested(document: TOMLValue) -> [TOMLValue] throw TOMLValueError {
    let members = document.members()?;
    return members[0].1.elements()?;
}
fn owned(document: TOMLValue) -> TOMLValue throw TOMLValueError {
    let members = document.members()?;
    return members[0].1;
}
)",
                modules
            ));
            constexpr auto rejected = std::to_array<std::string_view>({
                R"(fn invalid(&document: TOMLValue, replacement: TOMLValue) -> usize throw TOMLValueError {
    let members = document.members()?;
    document = replacement;
    return members.length();
})",
                R"(fn consume(&&document: TOMLValue) {}
fn invalid(input: TOMLValue) -> usize throw TOMLValueError {
    let document = input;
    let elements = document.members()?[0].1.elements()?;
    consume(&&document);
    return elements.length();
})",
                R"(fn invalid(text: str) -> [(String, TOMLValue)] throw TOMLError + TOMLValueError {
    return TOMLValue::parse(text)?.members()?;
})",
            });
            each(rejected, std::identity {}, [&](std::string_view source) noexcept {
                expect_diagnostic(
                    analyze_test_errors(std::string(imports) + std::string(source), modules),
                    DiagnosticCode::AccessBorrowConflict
                );
            });
        };


    "Standard collections: entry slices protect their map owner"_test = [] static noexcept {
        constexpr auto modules = std::to_array<std::string_view>({"collections"});
        constexpr auto imports = std::string_view("import std::collections using HashMap;\n");
        constexpr auto rejected = std::to_array<std::string_view>({
            R"(fn invalid() -> usize {
    var index = HashMap<String, i32>::new();
    index.insert(&&("first" as String), &&1);
    let records = index.entries();
    index.insert(&&("second" as String), &&2);
    return records.length();
})",
            R"(fn invalid() -> usize {
    var index = HashMap<String, i32>::new();
    index.insert(&&("first" as String), &&1);
    let records = index.entries();
    let moved = &&index;
    return records.length() + moved.length();
})",
        });
        each(rejected, std::identity {}, [&](std::string_view source) noexcept {
            expect_diagnostic(
                analyze_test_errors(std::string(imports) + std::string(source), modules),
                DiagnosticCode::AccessBorrowConflict
            );
        });
        static_cast<void>(analyze_test_program(
            std::string(imports) + R"(
fn valid() -> usize {
    var index = HashMap<String, i32>::new();
    index.insert(&&("first" as String), &&1);
    if true {
        let records = index.entries();
        assert(records.length() == 1 && records[0].value == 1);
    }
    index.insert(&&("second" as String), &&2);
    let moved = &&index;
    return moved.length();
}
)",
            modules
        ));
    };
});

}
