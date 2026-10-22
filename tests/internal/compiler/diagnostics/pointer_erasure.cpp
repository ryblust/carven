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
            {.name = "restoration cannot gain Write access",
             .source = "fn invalid(p: ptr<void>) { let q = p as ptr<&i32>; }",
             .code = DiagnosticCode::TypeCast,
             .primary_text = "as"},
        });
        check_compiler_errors(cases);
    };

    "Pointer storage identity: permission narrowing preserves callable contracts"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "implicit narrowing cannot widen pointee failures",
             .source =
                 "struct Failure {} fn target() -> i32 => 7; fn invalid() { var callable = target; let p = addressof(&callable); let q: ptr<fn() -> i32 throw Failure> = p; }",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "p"},
            {.name = "explicit narrowing cannot widen pointee failures",
             .source =
                 "struct Failure {} fn target() -> i32 => 7; fn invalid() { var callable = target; let p = addressof(&callable); let q = p as ptr<fn() -> i32 throw Failure>; }",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "as"},
            {.name = "identity cast cannot widen pointee failures",
             .source =
                 "struct Failure {} fn target() -> i32 => 7; fn invalid() { let callable = target; let p = addressof(callable); let q = p as ptr<fn() -> i32 throw Failure>; }",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "as"},
            {.name = "constant pointer context preserves pointee failures",
             .source =
                 "struct Failure {} const pointer: ptr<fn() -> i32 throw Failure> = nullptr as ptr<fn() -> i32>;",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "nullptr as ptr<fn() -> i32>"},
            {.name = "constant array elements preserve pointee failures",
             .source =
                 "struct Failure {} const pointers: [ptr<fn() -> i32 throw Failure>; 1] = [nullptr as ptr<fn() -> i32>];",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "nullptr as ptr<fn() -> i32>"},
            {.name = "constant slice elements preserve pointee failures",
             .source =
                 "struct Failure {} const pointers: [ptr<fn() -> i32 throw Failure>] = [nullptr as ptr<fn() -> i32>];",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "nullptr as ptr<fn() -> i32>"},
            {.name = "closure storage cannot become view storage",
             .source =
                 "fn invalid() { let captured = 7; var closure = [captured]() => captured; let p = addressof(&closure); let q: ptr<fn() -> i32> = p; }",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "p"},
            {.name = "array adaptation preserves pointer target failures",
             .source =
                 "struct Failure {} fn target() -> i32 => 7; fn invalid() { var callable = target; let source = [addressof(&callable)]; let changed: [ptr<&fn() -> i32 throw Failure>; 1] = source; }",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "source"},
            {.name = "effectful null operand cannot refine the other pointer slot",
             .source =
                 "fn target() -> i32 => 7; fn clear(&p: ptr<fn() -> i32>) -> ptr<fn() -> i32> { p = nullptr; return nullptr; } fn invalid() -> i32 { var callable = target; var p = addressof(callable); if p != clear(&p) { return (*p)(); } return 0; }",
             .code = DiagnosticCode::PointerNonNull,
             .primary_text = "*p"},
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
            {.name = "canonical array delivery does not freeze a non-null local pointer",
             .source =
                 "const fn target() -> i32 => 7; const fn addresses() -> [ptr<fn() -> i32>; 1] { let callable = target; return [addressof(callable)]; } const pointers = addresses();",
             .code = DiagnosticCode::ConstInitializer,
             .primary_text = "addresses()"},
            {.name = "erased static backing is not a frozen pointer value",
             .source = R"(const p = c"123" as ptr<void>;)",
             .code = DiagnosticCode::ConstInitializer,
             .primary_text = R"(c"123" as ptr<void>)"},
            {.name = "C string character backing is not a pointer object",
             .source = R"(
                const fn restore<T>(address: ptr<void>, witness: ptr<T>) -> ptr<T> {
                    let _ = witness;
                    return address as ptr<T>;
                }
                const {
                    let text = c"123";
                    let address = text as ptr<void>;
                    let pointer = restore(address, addressof(text));
                    if pointer != nullptr { println(*pointer); }
                }
             )",
             .code = DiagnosticCode::ConstEvaluation,
             .primary_text = "*pointer"},
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
            {.name = "empty tuple fields can overlap in native storage",
             .source =
                 "struct First {} struct Second {} const { let fields = (First {}, Second {}); println((addressof(fields.0) as ptr<void>) == (addressof(fields.1) as ptr<void>)); }",
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
                {.name = "indirect callable Write preserves storage loan checks",
                 .source = R"(
                    fn clear(&text: String, view: str) { let _ = view; text.clear(); }
                    fn invalid() {
                        let callback: fn(&String, str) -> void = clear;
                        let pointer = addressof(callback);
                        if pointer == nullptr { return; }
                        var text: String = "abc";
                        let borrowed = text.as_str();
                        (*pointer)(&text, borrowed);
                        println(borrowed);
                    }
                 )",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "(*pointer)(&text, borrowed)"},
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
});

} // namespace
