module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.formatted_append;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: formatted append requires interpolation and writable storage") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "immutable destination",
         .source = R"(fn bad() { let text = String::new(); text.append_format(f"{7}"); })",
         .code = "CV-ACCESS-IMMUTABLE",
         .primary_text = R"(text.append_format(f"{7}"))"},
        {.name = "Read destination",
         .source = R"(fn bad(text: String) { text.append_format(f""); })",
         .code = "CV-ACCESS-IMMUTABLE",
         .primary_text = R"(text.append_format(f""))"},
        {.name = "temporary destination",
         .source = R"(fn bad() { String::new().append_format(f"{7}"); })",
         .code = "CV-ACCESS-NOT-ASSIGNABLE",
         .primary_text = R"(String::new().append_format(f"{7}"))"},
        {.name = "missing interpolation",
         .source = R"(fn bad() { var text = String::new(); text.append_format(); })",
         .code = "CV-TYPE-METHOD-CALL-ARITY",
         .primary_text = "text.append_format()"},
        {.name = "extra interpolation",
         .source = R"(fn bad() { var text = String::new(); text.append_format(f"a", f"b"); })",
         .code = "CV-TYPE-METHOD-CALL-ARITY",
         .primary_text = R"(text.append_format(f"a", f"b"))"},
        {.name = "plain text is not interpolation syntax",
         .source = R"(fn bad() { var text = String::new(); text.append_format("value"); })",
         .code = "CV-TYPE-METHOD-CALL",
         .primary_text = R"("value")"},
        {.name = "an existing String is not interpolation syntax",
         .source =
             R"(fn bad() { var text = String::new(); let value = f"{7}"; text.append_format(value); })",
         .code = "CV-TYPE-METHOD-CALL",
         .primary_text = "value"},
        {.name = "access markers do not turn interpolation into a method argument",
         .source = R"(fn bad() { var text = String::new(); text.append_format(&f"{7}"); })",
         .code = "CV-TYPE-METHOD-CALL",
         .primary_text = R"(&f"{7}")"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: formatted append keeps destination separate from input storage") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "owning Read aliases destination",
         .source = R"(fn bad() { var text: String = "value"; text.append_format(f"{text}"); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(text.append_format(f"{text}"))"},
        {.name = "view aliases destination",
         .source =
             R"(fn bad() { var text: String = "value"; text.append_format(f"{text.as_str()}"); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(text.append_format(f"{text.as_str()}"))"},
        {.name = "named view blocks even empty append",
         .source =
             R"(fn bad() { var text: String = "value"; let view = text.as_str(); text.append_format(f""); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(text.append_format(f""))"},
        {.name = "callee maps String Read alias to destination",
         .source = R"(fn add(&text: String, input: String) { text.append_format(f"{input}"); }
                      fn bad() { var text: String = "value"; add(&text, text); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(text.append_format(f"{input}"))"},
        {.name = "callee maps view alias to destination",
         .source = R"(fn add(&text: String, input: str) { text.append_format(f"{input}"); }
                      fn bad() { var text: String = "value"; add(&text, text.as_str()); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(text.append_format(f"{input}"))"},
        {.name = "same projected String aliases destination",
         .source = R"(struct Holder { text: String }
                      fn bad() { var holder = Holder { text: String::new() }; holder.text.append_format(f"{holder.text}"); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(holder.text.append_format(f"{holder.text}"))"},
        {.name = "unknown array indices may overlap",
         .source =
             R"(fn bad(index: usize) { var values = [String::new(), String::new()]; values[index].append_format(f"{values[0]}"); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(values[index].append_format(f"{values[0]}"))"},
        {.name = "selected destination cannot be taken by a hole",
         .source = R"(fn consume(&&text: String) -> i32 => 7;
                      fn bad() { var text = String::new(); text.append_format(f"{consume(&&text)}"); })",
         .code = "CV-ACCESS-OPERATION-CONFLICT",
         .primary_text = "&&text"},
        {.name = "earlier view blocks mutation in a later hole",
         .source = R"(fn change(&text: String) -> i32 { text.clear(); return 7; }
                      fn bad() { var text = String::new(); var input = String::new(); text.append_format(f"{input.as_str()}/{change(&input)}"); })",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "text.clear()"},
        {.name = "constant execution does not bypass alias checks",
         .source =
             R"(const fn make() -> String { var text: String = "value"; text.append_format(f"{text}"); return text; } const result = make();)",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = R"(text.append_format(f"{text}"))"},
        {.name = "required formatting reports unsupported specifications at append",
         .source =
             R"(const fn make() -> String { var text = String::new(); text.append_format(f"{7:+}"); return text; } const result = make();)",
         .code = "CV-CONST-EVALUATION",
         .primary_text = R"(text.append_format(f"{7:+}"))"},
    });
    check_compiler_errors(cases);
}
