module carven:test.internal.compiler.diagnostics.utf;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "UTF craft: returned text protects input storage from mutation"_test = [] static noexcept {
        const auto modules = std::to_array<std::string_view>({
            "utf.error",
            "utf.scan",
            "utf.block",
            "utf.validation",
            "utf.text",
            "simd.bytes",
        });
        with_compiled_source(
            "import std::utf.text using from_utf8; import std::utf.error using UTF8Error; "
            "fn bad() throw UTF8Error { var a: [u8; 1] = [65]; let text = from_utf8(a)?; a[0] = 66; }",
            [](const auto& sources, const auto& result) static noexcept {
                if (!expect(!result.has_value())
                    || !expect_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict)) {
                    return;
                }
                const auto* diagnostic =
                    find_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict);
                if (!expect(diagnostic->attachment.primary.has_value())) {
                    return;
                }
                expect_equal(sources.slice(diagnostic->attachment.primary->span), "a[0] = 66");
            },
            modules
        );
    };

    "UTF craft: encoded views protect encoding storage from mutation"_test = [] static noexcept {
        const auto modules = std::to_array<std::string_view>({
            "utf.error",
            "utf.codec",
            "utf.scan",
            "utf.block",
            "simd.bytes",
        });
        with_compiled_source(
            "import std::utf.codec using { encode_utf8, encoded_bytes }; "
            "fn bad() { var encoded = encode_utf8('a'); let bytes = encoded_bytes(encoded); "
            "encoded = encode_utf8('b'); }",
            [](const auto& sources, const auto& result) static noexcept {
                if (!expect(!result.has_value())
                    || !expect_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict)) {
                    return;
                }
                const auto* diagnostic =
                    find_diagnostic(result.error(), DiagnosticCode::AccessBorrowConflict);
                if (!expect(diagnostic->attachment.primary.has_value())) {
                    return;
                }
                expect(!sources.slice(diagnostic->attachment.primary->span).empty());
            },
            modules
        );
    };

    "Compiler diagnostics: unchecked text construction checks types and backing"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "scalar input must be u32",
                 .source = "fn bad(v: i32) { let c = char::from_u32_unchecked(v); }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "v"},
                {.name = "text input must contain bytes",
                 .source = "fn bad(v: [i32]) { let s = str::from_utf8_unchecked(v); }",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "v"},
                {.name = "text construction requires one argument",
                 .source = "fn bad() { let s = str::from_utf8_unchecked(); }",
                 .code = DiagnosticCode::TypeMethodCallArity,
                 .primary_text = "str::from_utf8_unchecked()"},
                {.name = "text construction cannot return local storage",
                 .source =
                     "fn bad() -> str { let a: [u8; 1] = [65]; return str::from_utf8_unchecked(a); }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "a"},
                {.name = "borrowed text prevents String mutation",
                 .source =
                     "fn bad() { var s: String = \"hello\"; let text = str::from_utf8_unchecked(s.bytes); s.clear(); }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "s.clear()"},
            });
            check_compiler_errors(cases);
        };

    "Character construction: static execution diagnoses invalid scalar preconditions"_test =
        [] static noexcept {
            static constexpr auto cases = std::array {0xd800u, 0xdfffu, 0x110000u, 0xffffffffu};
            each(
                cases,
                [](auto value) static noexcept { return std::format("value: {}", value); },
                [](auto value) static noexcept {
                    const auto call = std::format("char::from_u32_unchecked({}u32)", value);
                    check_compiler_error(
                        std::format("const invalid = {};", call),
                        DiagnosticCode::ConstEvaluation,
                        std::string_view(call)
                    );
                    check_compiler_error(
                        std::format(
                            "const fn make(value: u32) -> char => char::from_u32_unchecked(value); "
                            "const invalid = make({}u32);",
                            value
                        ),
                        DiagnosticCode::ConstEvaluation,
                        "char::from_u32_unchecked(value)"
                    );
                }
            );
        };

    "Text construction: static execution diagnoses invalid UTF8 preconditions"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "invalid leading byte",
                 .source = "const fn invalid() -> String { let bytes = [0xffu8]; "
                           "return str::from_utf8_unchecked(bytes) as String; } "
                           "const text = invalid();",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "str::from_utf8_unchecked(bytes)"},
                {.name = "incomplete scalar at selected range end",
                 .source =
                     "const fn invalid() -> String { let bytes = [0x61u8, 0xe2, 0x82, 0xac]; "
                     "return str::from_utf8_unchecked(bytes.as_slice().slice(1, 3)) as String; } "
                     "const text = invalid();",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "str::from_utf8_unchecked(bytes.as_slice().slice(1, 3))"},
            });
            check_compiler_errors(cases);
        };
});

} // namespace
