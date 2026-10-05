module carven:test.internal.compiler.diagnostics.pointer_erasure;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Pointer erasure: object casts preserve access and target legality"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "Read to Write erasure",
             .source = "fn invalid(p: ptr<i32>) { let q = p as ptr<&void>; }",
             .code = DiagnosticCode::TypeCast,
             .primary_text = "as"},
            {.name = "pending pointee cannot gain Write access",
             .source =
                 "fn target() -> i32 => 7; fn invalid() { let value = target; let p = addressof(value); let q = p as ptr<&void>; }",
             .code = DiagnosticCode::TypeCast,
             .primary_text = "as"},
            {.name = "C string cannot gain Write access",
             .source = R"(fn invalid() { let q = c"123" as ptr<&void>; })",
             .code = DiagnosticCode::TypeCast,
             .primary_text = "as"},
            {.name = "untyped restoration",
             .source = "fn invalid(p: ptr<void>) { let q = p as ptr<i32>; }",
             .code = DiagnosticCode::TypeCast,
             .primary_text = "as"},
        });
        check_compiler_errors(cases);
    };

    "Pointer execution: publication and dereference respect storage boundaries"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "local pointer escape",
             .source =
                 "const fn address() -> ptr<void> { let value = 1; return addressof(value) as ptr<void>; } const p = address();",
             .code = DiagnosticCode::ConstInitializer,
             .primary_text = "address()"},
            {.name = "erased static backing is not a frozen pointer value",
             .source = R"(const p = c"123" as ptr<void>;)",
             .code = DiagnosticCode::ConstInitializer,
             .primary_text = R"(c"123" as ptr<void>)"},
            {.name = "typed pointer needs explicit erasure for formatting",
             .source = R"(const { let value = 1; let p = addressof(value); println(f"{p:p}"); })",
             .code = DiagnosticCode::ConstEvaluation,
             .primary_text = R"(f"{p:p}")"},
            {.name = "unsupported pointer format",
             .source =
                 R"(const { let value = 1; let p = addressof(value) as ptr<void>; println(f"{p:x}"); })",
             .code = DiagnosticCode::ConstEvaluation,
             .primary_text = R"(f"{p:x}")"},
            {.name = "array container address relationship needs layout",
             .source =
                 "const { let values = [1, 2]; let whole = addressof(values) as ptr<void>; let first = addressof(values[0]) as ptr<void>; println(whole == first); }",
             .code = DiagnosticCode::ConstEvaluation,
             .primary_text = "=="},
            {.name = "text backing and its String owner need an address relation",
             .source =
                 R"(const { let text: String = "abc"; let bytes = text.bytes; let owner = addressof(text) as ptr<void>; let first = addressof(bytes[0]) as ptr<void>; println(owner == first); })",
             .code = DiagnosticCode::ConstEvaluation,
             .primary_text = "=="},
            {.name = "retained text buffers do not prove distinct native addresses",
             .source =
                 R"(const { let a = "abc"; let b = "bc"; println(addressof(a.bytes[1]) == addressof(b.bytes[0])); })",
             .code = DiagnosticCode::ConstEvaluation,
             .primary_text = "=="},
            {.name = "pointer display does not establish target liveness",
             .source =
                 R"(const { var p: ptr<i32> = nullptr; if true { let value = 1; p = addressof(value); } if p != nullptr { println(f"{p as ptr<void>:p}"); println(*p); } })",
             .code = DiagnosticCode::ConstEvaluation,
             .primary_text = "*p"},
        });
        check_compiler_errors(cases);
    };

    "Indirect callable access: existing views continue without establishing new borrows"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "existing view read through a pointer preserves subsequent access checks",
                 .source = R"(
                    fn target() -> i32 => 7;
                    fn consume(&&value: i32) {}
                    fn invalid() {
                        let view: fn() -> i32 = target;
                        let pointer = addressof(view);
                        let value = 1;
                        consume(&&value);
                        if pointer == nullptr { return; }
                        let copy = *pointer;
                        copy();
                        println(value);
                    }
                 )",
                 .code = DiagnosticCode::AccessUnavailable,
                 .primary_text = "value"},
                {.name = "existing view read through a slice preserves subsequent access checks",
                 .source = R"(
                    fn consume(&&value: i32) {}
                    fn invalid(values: [fn() -> i32]) {
                        let value = 1;
                        consume(&&value);
                        let copy = values[0];
                        copy();
                        println(value);
                    }
                 )",
                 .code = DiagnosticCode::AccessUnavailable,
                 .primary_text = "value"},
                {.name = "nested callable array copies preserve subsequent access checks",
                 .source = R"(
                    fn consume(&&value: i32) {}
                    fn invalid(values: [[fn() -> i32; 1]]) {
                        let value = 1;
                        consume(&&value);
                        let copy = values[0];
                        copy[0]();
                        println(value);
                    }
                 )",
                 .code = DiagnosticCode::AccessUnavailable,
                 .primary_text = "value"},
                {.name = "indirect callable slice iteration preserves body access checks",
                 .source = R"(
                    fn consume(&&value: i32) {}
                    fn invalid(values: [fn() -> i32]) {
                        let pointer = addressof(values);
                        let value = 1;
                        consume(&&value);
                        if pointer == nullptr { return; }
                        for callable in *pointer {
                            callable();
                            println(value);
                        }
                    }
                 )",
                 .code = DiagnosticCode::AccessUnavailable,
                 .primary_text = "value"},
                {.name = "indirect closure calls preserve subsequent access checks",
                 .source = R"(
                    fn consume(&&value: i32) {}
                    fn invalid() {
                        var target = 0;
                        let closure = [&target]() { target += 1; };
                        let pointer = addressof(closure);
                        let value = 1;
                        consume(&&value);
                        if pointer == nullptr { return; }
                        (*pointer)();
                        let copied = *pointer;
                        copied();
                        println(value);
                    }
                 )",
                 .code = DiagnosticCode::AccessUnavailable,
                 .primary_text = "value"},
                {.name = "known capture writes remain checked after an opaque target join",
                 .source = R"(
                    fn invalid(choose: bool) {
                        var text: String = "abc";
                        let known = [&text](input: str) { text.clear(); };
                        var selected = known;
                        let pointer = addressof(known);
                        if pointer == nullptr { return; }
                        if choose { selected = *pointer; }
                        selected(text.as_str());
                    }
                 )",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "text.clear()"},
                {.name = "an indirectly read closure cannot establish a new callable borrow",
                 .source = R"(
                    fn invalid() {
                        let captured = 7;
                        let closure = [captured]() -> i32 => captured;
                        let pointer = addressof(closure);
                        if pointer == nullptr { return; }
                        let view: fn() -> i32 = *pointer;
                    }
                 )",
                 .code = DiagnosticCode::TypeCallableViewEscape,
                 .primary_text = "*pointer"},
            });
            check_compiler_errors(cases);
        };

    "Callable execution: live-backed view widening requires execution support"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "scalar view widening borrows its source object",
                 .source = R"(
                struct Failure {}
                const fn target() -> i32 => 1;
                const {
                    let source: fn() -> i32 = target;
                    let widened: fn() -> i32 throw Failure = source;
                }
             )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "source"},
                {.name = "array view widening borrows its source elements",
                 .source = R"(
                struct Failure {}
                const fn target() -> i32 => 1;
                const {
                    let source: [fn() -> i32; 1] = [target];
                    let widened: [fn() -> i32 throw Failure; 1] = source;
                }
             )",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = "source"},
            });
            check_compiler_errors(cases);
            check_compiler_accepts(R"(
                struct Failure {}
                const {
                    let source: [fn() -> i32; 0] = [];
                    let widened: [fn() -> i32 throw Failure; 0] = source;
                    assert(widened.as_slice().len() == 0);
                }
            )");
        };

    "Pointer formatting: static text can be published independently of output"_test =
        [] static noexcept {
            check_compiler_accepts(R"(
            const fn label() -> String {
                let value = 1;
                let p = addressof(value) as ptr<void>;
                return f"{p:p}";
            }
            const text = label();
            const absent = f"{(nullptr as ptr<void>):p}";
            const fn selected(const label: str) { assert(label == "const@nonnull"); }
            const {
                let value = 1;
                let p = addressof(value) as ptr<void>;
                println(f"{p:p}");
                const stable = 7;
                assert(stable == 7);
                let formatted = f"{p:p}";
                const saved = formatted;
                const count = formatted.len();
                assert(count == text.len());
                selected(saved);
                assert(absent == "const@null");
            }
        )");
        };
});

} // namespace
