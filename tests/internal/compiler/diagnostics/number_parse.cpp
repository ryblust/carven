module carven:test.internal.compiler.diagnostics.number_parse;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

constexpr auto standard_modules = std::to_array<std::string_view>({"core"});

const TestSuite suite([] static noexcept {
    "Numeric parsing: naming the standard error requires an import"_test = [] static noexcept {
        check_compiler_error(
            "fn parse(text: str) -> i32 throw NumberParseError => i32::parse(text)?;",
            DiagnosticCode::TypeUnresolved,
            "NumberParseError",
            standard_modules
        );
    };

    "Numeric parsing: standard and local errors retain distinct identities"_test =
        [] static noexcept {
            check_compiler_error(
                R"(
import std::core using NumberParseError as ParseError;
enum NumberParseError { Local }
fn wrong() -> NumberParseError => ParseError::Invalid;
)",
                DiagnosticCode::TypeMismatch,
                "ParseError::Invalid",
                standard_modules
            );
        };

    "Numeric parsing: factory calls require text Read access and failure handling"_test =
        [] static noexcept {
            const auto cases = std::array {
                std::pair {"fn bad() { i32::parse(); }", DiagnosticCode::TypeMethodCallArity},
                std::pair {"fn bad() { f32::parse(1); }", DiagnosticCode::TypeMismatch},
                std::pair {
                    "fn bad(&text: str) { i32::parse(&text)?; }",
                    DiagnosticCode::AccessCallMismatch
                },
                std::pair {"fn bad() { i32::parse(\"1\"); }", DiagnosticCode::EffectUnmarked},
            };
            for (const auto& [source, code] : cases) {
                with_compiled_source(
                    source,
                    [code](const auto&, const auto& result) noexcept {
                        if (expect(!result.has_value())) {
                            expect_diagnostic(result.error(), code);
                        }
                    },
                    standard_modules
                );
            }
        };

    "Numeric parsing: unhandled static failure follows source failure diagnostics"_test =
        [] static noexcept {
            with_compiled_source(
                "const value = u8::parse(\"256\")?;",
                [](const auto&, const auto& result) static noexcept {
                    if (expect(!result.has_value())) {
                        expect_diagnostic(result.error(), DiagnosticCode::ConstEvaluation);
                    }
                },
                standard_modules
            );
        };
});

} // namespace
