module carven:test.internal.compiler.diagnostics.math;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Floating methods: static clamp rejects reversed and unordered bounds"_test =
        [] static noexcept {
            for (const auto source : {
                     "const value = 0.0f32.clamp(2.0, 1.0);",
                     "const fn bad() { f32x4::splat(0.0).clamp(0.0, "
                     "f32x4::splat(1.0).with_lane(3, 0.0 / 0.0)); } const { bad(); }",
                 }) {
                with_compiled_source(source, [](const auto&, const auto& result) static noexcept {
                    if (expect(!result.has_value())) {
                        expect_diagnostic(result.error(), DiagnosticCode::AssertionFailed);
                    }
                });
            }
        };

    "Floating methods: calls enforce arity precision and Read access"_test = [] static noexcept {
        const auto cases = std::array {
            std::pair {"fn bad(v: f32) { v.abs(1.0); }", DiagnosticCode::TypeMethodCallArity},
            std::pair {"fn bad(v: f64) { v.min(); }", DiagnosticCode::TypeMethodCallArity},
            std::pair {"fn bad(v: f32x8) { v.clamp(1.0); }", DiagnosticCode::TypeMethodCallArity},
            std::pair {
                "fn bad(v: f32x8, other: f64) { v.min(other); }",
                DiagnosticCode::TypeMismatch
            },
            std::pair {
                "fn bad(v: f32x4, other: f32x8) { v.clamp(0.0, other); }",
                DiagnosticCode::TypeMismatch
            },
            std::pair {
                "fn bad(v: f32, other: f64) { v.min(other); }",
                DiagnosticCode::TypeMismatch
            },
            std::pair {
                "fn bad(v: f32, &other: f32) { v.min(&other); }",
                DiagnosticCode::AccessCallMismatch
            },
            std::pair {
                "fn bad() { var s: String = \"text\"; let view = s.as_str(); "
                "1.0f32.min(if true { s.clear(); 0.0f32 } else { 0.0f32 }); println(view); }",
                DiagnosticCode::AccessBorrowConflict
            },
        };
        for (const auto& [source, code] : cases) {
            with_compiled_source(source, [code](const auto&, const auto& result) noexcept {
                if (expect(!result.has_value())) {
                    expect_diagnostic(result.error(), code);
                }
            });
        }
    };
});

} // namespace
