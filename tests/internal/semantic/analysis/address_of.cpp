module carven:test.internal.semantic.analysis.address_of;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Address-of: declared callable and aggregate target types normalize without solving"_test =
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(R"(
                struct Failure {}
                fn identity(value: i32) -> i32 => value;
                fn declared_failure(callback: fn(i32) -> i32 throw Failure) {
                    let pointer = addressof(callback);
                    let _ = pointer;
                }
                fn declared() {
                    var callback: fn(i32) -> i32 = identity;
                    let readonly = addressof(callback);
                    let writable = addressof(&callback);
                    if readonly != nullptr { let _ = readonly; }
                    if writable != nullptr { let _ = writable; }
                    var callbacks: [fn(i32) -> i32; 2] = [identity, identity];
                    let array_pointer = addressof(&callbacks);
                    if array_pointer != nullptr { let _ = array_pointer; }
                }
            )");
            expect(diagnostics.empty());
        };

    "Address-of: indirect callable Read cannot establish a tracked borrow"_test =
        [] static noexcept {
            const auto sources = std::array<std::string_view, 3> {
                R"(
                    fn identity(value: i32) -> i32 => value;
                    fn rejected() {
                        let callback: fn(i32) -> i32 = identity;
                        let pointer = addressof(callback);
                        if pointer != nullptr { let selected = *pointer; let _ = selected; }
                    }
                )",
                R"(
                    fn identity(value: i32) -> i32 => value;
                    fn rejected() {
                        let callback: fn(i32) -> i32 = identity;
                        let pointer = addressof(callback);
                        if pointer != nullptr { let result = (*pointer)(1); let _ = result; }
                    }
                )",
                R"(
                    fn identity(value: i32) -> i32 => value;
                    fn rejected() {
                        let callbacks: [fn(i32) -> i32; 2] = [identity, identity];
                        let pointer = addressof(callbacks);
                        if pointer != nullptr { let selected = (*pointer)[0]; let _ = selected; }
                    }
                )",
            };
            for (const auto source : sources) {
                expect_diagnostic(
                    analyze_test_errors(std::string(source)),
                    DiagnosticCode::TypeCallableViewEscape
                );
            }
        };

    "Address-of: callable indexing retains tracked array and slice backing"_test =
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(R"(
                fn identity(value: i32) -> i32 => value;
                fn tracked() {
                    let callbacks: [fn(i32) -> i32; 2] = [identity, identity];
                    let selected = callbacks[0];
                    let views: [fn(i32) -> i32] = callbacks;
                    let sliced = views[1];
                    let temporary = [identity, identity][0];
                    let _ = selected(1) + sliced(2) + temporary(3);
                }
            )");
            expect(diagnostics.empty());
        };

    "Address-of: indirect callable Write cannot establish a tracked borrow"_test =
        [] static noexcept {
            const auto sources = std::array<std::string_view, 2> {
                R"(
                    fn identity(value: i32) -> i32 => value;
                    fn rejected() {
                        var callback: fn(i32) -> i32 = identity;
                        let pointer = addressof(&callback);
                        if pointer != nullptr { *pointer = [](value) => value + 1; }
                    }
                )",
                R"(
                    fn identity(value: i32) -> i32 => value;
                    fn replace(&callback: fn(i32) -> i32) { callback = identity; }
                    fn rejected() {
                        var callback: fn(i32) -> i32 = identity;
                        let pointer = addressof(&callback);
                        if pointer != nullptr { replace(&*pointer); }
                    }
                )",
            };
            for (const auto source : sources) {
                expect_diagnostic(
                    analyze_test_errors(std::string(source)),
                    DiagnosticCode::TypeCallableViewEscape
                );
            }
        };

    "Address-of: an inferred callable failure contract is not frozen by taking an address"_test =
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(R"(
                fn identity(value: i32) -> i32 => value;
                fn inferred() {
                    let callback = identity;
                    let pointer = addressof(callback);
                    let _ = pointer;
                }
            )");
            expect_diagnostic(diagnostics, DiagnosticCode::TypeUnresolved);
        };
});

} // namespace
