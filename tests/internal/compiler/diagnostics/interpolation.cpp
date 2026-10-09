module carven:test.internal.compiler.diagnostics.interpolation;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Compiler diagnostics: interpolation consumes Read operands and protects backing"_test = [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "Write marker",
             .source = R"(fn bad() { var x = 1; let s = f"{&x}"; })",
             .code = DiagnosticCode::AccessCallMismatch,
             .primary_text = "&x"},
            {.name = "Take marker",
             .source = R"(fn bad() { let x = 1; let s = f"{&&x}"; })",
             .code = DiagnosticCode::AccessCallMismatch,
             .primary_text = "&&x"},
            {.name = "known local is still runtime storage",
             .source = R"(fn bad() { let value = 42; const copy = value; })",
             .code = DiagnosticCode::ConstInitializer,
             .primary_text = "const copy = value"},
            {.name = "known local remains unavailable after Take",
             .source =
                 R"(fn take(&&value: i32) {} fn bad() { let value = 42; take(&&value); let text = f"{value}"; })",
             .code = DiagnosticCode::AccessUnavailable,
             .primary_text = "value"},
            {.name = "pending view mutation",
             .source =
                 R"(fn change(&s: String) -> i32 { s.clear(); return 1; } fn bad() { var s = String {}; let r = f"{s.as_str()}{change(&s)}"; })",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "dynamic width protects text",
             .source =
                 R"(fn change(&s: String) -> i32 { s.clear(); return 1; } fn bad() { var s = String {}; let r = f"{s.as_str():>{change(&s)}}"; })",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "pending owner Take",
             .source =
                 R"(fn consume(&&s: String) -> usize => s.byte_length(); fn bad() { var s = String {}; let r = f"{s}{consume(&&s)}"; })",
             .code = DiagnosticCode::AccessOperationConflict,
             .primary_text = "&&s"},
            {.name = "named holder remains after formatting",
             .source =
                 R"(fn bad() { var s = String {}; let v = s.as_str(); let r = f"{v}"; s.clear(); })",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "temporary result view cannot be saved",
             .source = R"(fn bad() { let v = f"plain".as_str(); })",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = R"(let v = f"plain".as_str())"},
            {.name = "callable view boundary",
             .source = R"(fn bad(callback: fn() -> void) { let s = f"{callback}"; })",
             .code = DiagnosticCode::TypeCallableViewEscape,
             .primary_text = R"(f"{callback}")"},
            {.name = "Write capture boundary",
             .source =
                 R"(fn bad() { var x = 1; let callback = [&x]() { x += 1; }; let s = f"{callback}"; })",
             .code = DiagnosticCode::TypeCallableViewEscape,
             .primary_text = R"(f"{callback}")"},
        });
        check_compiler_errors(cases);
    };

    "Compiler diagnostics: constant interpolation preserves access type and execution boundaries"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "constant Write marker",
                 .source = R"(const x = 1; const text = f"{&x}";)",
                 .code = DiagnosticCode::AccessCallMismatch,
                 .primary_text = "&x"},
                {.name = "constant Take marker",
                 .source = R"(const x = 1; const text = f"{&&x}";)",
                 .code = DiagnosticCode::AccessCallMismatch,
                 .primary_text = "&&x"},
                {.name = "nested width access",
                 .source = R"(const width = 4; const text = f"{42:0{&width}}";)",
                 .code = DiagnosticCode::AccessCallMismatch,
                 .primary_text = "&width"},
                {.name = "text factory access",
                 .source = R"(const text = String::from_str(&"x");)",
                 .code = DiagnosticCode::AccessCallMismatch,
                 .primary_text = R"(&"x")"},
                {.name = "text factory argument",
                 .source = R"(const text = String::from_str(42);)",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = "42"},
                {.name = "unsupported specification",
                 .source = R"(const text = f"{42:+04}";)",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = R"(f"{42:+04}")"},
                {.name = "dynamic width remains typed",
                 .source = R"(const text = f"{42:0{"4"}}";)",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = R"(f"{42:0{"4"}}")"},
                {.name = "negative dynamic width",
                 .source = R"(const text = f"{42:{-1}}";)",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = R"(f"{42:{-1}}")"},
                {.name = "runtime binding is not a constant",
                 .source = R"(fn bad() { let x = 1; const text = f"{x}"; })",
                 .code = DiagnosticCode::ConstInitializer,
                 .primary_text = R"(const text = f"{x}")"},
                {.name = "owning text remains typed in nested calls",
                 .source =
                     R"(const fn consume(value: i32) -> i32 => value; const result = consume(f"x");)",
                 .code = DiagnosticCode::TypeMismatch,
                 .primary_text = R"(f"x")"},
                {.name = "direct formatting byte budget",
                 .source = R"(const text = f"x{1:1048576}";)",
                 .code = DiagnosticCode::ConstLimit,
                 .primary_text = R"(f"x{1:1048576}")"},
            });
            check_compiler_errors(cases);
        };

    "Compiler diagnostics: constant widths retain format grammar and resource limits"_test =
        [] static noexcept {
            const auto sources = std::to_array<std::string_view>({
                R"(const text = f"{7:2{2}}";)",
                R"(const text = f"{7:{2}{3}}";)",
                R"(const text = f"{7:{0}4}";)",
                R"(const text = f"{7:{2}0}";)",
                R"(const fn bad() -> String => f"{7:2{2}}"; const text = bad();)",
                R"(const fn bad() -> String => f"{7:{2}{3}}"; const text = bad();)",
                R"(const fn bad() -> String => f"{7:{0}4}"; const text = bad();)",
            });
            each(sources, std::identity {}, [&](const auto& source) noexcept {
                const auto start = source.find("f\"");
                const auto finish = source.find("\";", start) + 1uz;
                const auto cases = std::array {CompilerErrorExpectation {
                    .name = "invalid width structure",
                    .source = source,
                    .code = DiagnosticCode::ConstEvaluation,
                    .primary_text = source.substr(start, finish - start),
                }};
                check_compiler_errors(cases);
            });
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "direct field text budget",
                 .source = R"(const text = f"{1:1048577}";)",
                 .code = DiagnosticCode::ConstLimit,
                 .primary_text = R"(f"{1:1048577}")"},
                {.name = "constant function field text budget",
                 .source = R"(const fn large() -> String => f"{1:1048577}"; const text = large();)",
                 .code = DiagnosticCode::ConstLimit,
                 .primary_text = R"(f"{1:1048577}")"},
            });
            check_compiler_errors(cases);
            const auto initializer =
                std::format("String::from_str(\"{}\")", std::string(1048577uz, 'x'));
            const auto source = std::format("const length = {}.byte_length();", initializer);
            const auto large_text = std::array {CompilerErrorExpectation {
                .name = "direct text construction budget",
                .source = source,
                .code = DiagnosticCode::ConstLimit,
                .primary_text = initializer,
            }};
            check_compiler_errors(large_text);
        };

    "Compiler diagnostics: formatted append requires interpolation and writable storage"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "immutable destination",
                 .source = R"(fn bad() { let text = String {}; text.append_format(f"{7}"); })",
                 .code = DiagnosticCode::AccessImmutable,
                 .primary_text = R"(text.append_format(f"{7}"))"},
                {.name = "Read destination",
                 .source = R"(fn bad(text: String) { text.append_format(f""); })",
                 .code = DiagnosticCode::AccessImmutable,
                 .primary_text = R"(text.append_format(f""))"},
                {.name = "temporary destination",
                 .source = R"(fn bad() { String {}.append_format(f"{7}"); })",
                 .code = DiagnosticCode::AccessNotAssignable,
                 .primary_text = R"(String {}.append_format(f"{7}"))"},
                {.name = "missing interpolation",
                 .source = R"(fn bad() { var text = String {}; text.append_format(); })",
                 .code = DiagnosticCode::TypeMethodCallArity,
                 .primary_text = "text.append_format()"},
                {.name = "extra interpolation",
                 .source = R"(fn bad() { var text = String {}; text.append_format(f"a", f"b"); })",
                 .code = DiagnosticCode::TypeMethodCallArity,
                 .primary_text = R"(text.append_format(f"a", f"b"))"},
                {.name = "plain text is not interpolation syntax",
                 .source = R"(fn bad() { var text = String {}; text.append_format("value"); })",
                 .code = DiagnosticCode::TypeMethodCall,
                 .primary_text = R"("value")"},
                {.name = "an existing String is not interpolation syntax",
                 .source =
                     R"(fn bad() { var text = String {}; let value = f"{7}"; text.append_format(value); })",
                 .code = DiagnosticCode::TypeMethodCall,
                 .primary_text = "value"},
                {.name = "access markers do not turn interpolation into a method argument",
                 .source = R"(fn bad() { var text = String {}; text.append_format(&f"{7}"); })",
                 .code = DiagnosticCode::TypeMethodCall,
                 .primary_text = R"(&f"{7}")"},
            });
            check_compiler_errors(cases);
        };

    "Compiler diagnostics: formatted append keeps destination separate from input storage"_test =
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "owning Read aliases destination",
                 .source =
                     R"(fn bad() { var text: String = "value"; text.append_format(f"{text}"); })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = R"(text.append_format(f"{text}"))"},
                {.name = "view aliases destination",
                 .source =
                     R"(fn bad() { var text: String = "value"; text.append_format(f"{text.as_str()}"); })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = R"(text.append_format(f"{text.as_str()}"))"},
                {.name = "named view blocks even empty append",
                 .source =
                     R"(fn bad() { var text: String = "value"; let view = text.as_str(); text.append_format(f""); })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = R"(text.append_format(f""))"},
                {.name = "callee maps String Read alias to destination",
                 .source =
                     R"(fn add(&text: String, input: String) { text.append_format(f"{input}"); }
                      fn bad() { var text: String = "value"; add(&text, text); })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = R"(text.append_format(f"{input}"))"},
                {.name = "callee maps view alias to destination",
                 .source = R"(fn add(&text: String, input: str) { text.append_format(f"{input}"); }
                      fn bad() { var text: String = "value"; add(&text, text.as_str()); })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = R"(text.append_format(f"{input}"))"},
                {.name = "same projected String aliases destination",
                 .source = R"(struct Holder { text: String }
                      fn bad() { var holder = Holder { text: String {} }; holder.text.append_format(f"{holder.text}"); })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = R"(holder.text.append_format(f"{holder.text}"))"},
                {.name = "unknown array indices may overlap",
                 .source =
                     R"(fn bad(index: usize) { var values = [String {}, String {}]; values[index].append_format(f"{values[0]}"); })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = R"(values[index].append_format(f"{values[0]}"))"},
                {.name = "selected destination cannot be taken by a hole",
                 .source = R"(fn consume(&&text: String) -> i32 => 7;
                      fn bad() { var text = String {}; text.append_format(f"{consume(&&text)}"); })",
                 .code = DiagnosticCode::AccessOperationConflict,
                 .primary_text = "&&text"},
                {.name = "earlier view blocks mutation in a later hole",
                 .source = R"(fn change(&text: String) -> i32 { text.clear(); return 7; }
                      fn bad() { var text = String {}; var input = String {}; text.append_format(f"{input.as_str()}/{change(&input)}"); })",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "text.clear()"},
                {.name = "static execution does not bypass alias checks",
                 .source =
                     R"(const fn make() -> String { var text: String = "value"; text.append_format(f"{text}"); return text; } const result = make();)",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = R"(text.append_format(f"{text}"))"},
                {.name = "required formatting reports unsupported specifications at append",
                 .source =
                     R"(const fn make() -> String { var text = String {}; text.append_format(f"{7:+}"); return text; } const result = make();)",
                 .code = DiagnosticCode::ConstEvaluation,
                 .primary_text = R"(text.append_format(f"{7:+}"))"},
            });
            check_compiler_errors(cases);
        };
});

} // namespace
