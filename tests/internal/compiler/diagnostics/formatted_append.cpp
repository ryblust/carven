module carven:test.internal.compiler.diagnostics.formatted_append;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
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
