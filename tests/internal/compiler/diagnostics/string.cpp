module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.compiler.diagnostics.string;

import :test.internal.compiler.diagnostics.fixture;
import std;

TEST_CASE("Compiler diagnostics: String access and operation contracts") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "named view blocks mutation",
         .source =
             "fn invalid() { var s = String::from_str(\"abc\"); let v = s.as_str(); s.push('!'); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.push('!')"},
        {.name = "view copy keeps backing",
         .source =
             "fn invalid() { var s = String::from_str(\"abc\"); var v = s.as_str(); let copy = v; v = \"\"; s.clear(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "view blocks whole replacement",
         .source =
             "fn invalid() { var s = String::from_str(\"abc\"); let v = s.as_str(); s = String::new(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s = String::new()"},
        {.name = "view blocks Take",
         .source =
             "fn invalid() { var s = String::from_str(\"abc\"); let v = s.as_str(); let taken = &&s; }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "&&"},
        {.name = "self append is rejected",
         .source = "fn invalid() { var s = String::from_str(\"abc\"); s.append(s.as_str()); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.append(s.as_str())"},
        {.name = "pending view argument protects owner",
         .source =
             "fn consume(v: str, &&s: String) {} fn invalid() { var s = String::from_str(\"abc\"); consume(s.as_str(), &&s); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "&&s"},
        {.name = "temporary view cannot initialize holder",
         .source = "fn invalid() { let v = String::from_str(\"abc\").as_str(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "let v = String::from_str(\"abc\").as_str()"},
        {.name = "stored range protects owner",
         .source =
             "fn invalid() { var s = String::from_str(\"abc\"); let range = s.chars; s.clear(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "loop source protects owner",
         .source =
             "fn invalid() { var s = String::from_str(\"abc\"); for c in s.chars { s.clear(); } }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "aggregate view protects owner",
         .source =
             "struct View { text: str } fn invalid() { var s = String::from_str(\"abc\"); let v = View { text: s.as_str() }; s.clear(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "unknown array index overlaps",
         .source =
             "fn mutate(&values: [String; 2], index: usize) { values[index].clear(); } fn invalid() { var values = [String::new(), String::new()]; let v = values[0].as_str(); mutate(&values, 1); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "values[index].clear()"},
        {.name = "callee mutation checks caller holder",
         .source =
             "fn mutate(&s: String) { s.clear(); } fn invalid() { var s = String::from_str(\"abc\"); let v = s.as_str(); mutate(&s); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "self reference aggregate",
         .source =
             "struct Mixed { text: String, view: str } fn invalid() { var mixed = Mixed { text: String::new(), view: \"\" }; mixed.view = mixed.text.as_str(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "mixed.view = mixed.text.as_str()"},
        {.name = "immutable receiver",
         .source = "fn invalid() { let s = String::new(); s.clear(); }",
         .code = "CV-ACCESS-IMMUTABLE",
         .primary_text = "s.clear()"},
        {.name = "temporary receiver cannot write",
         .source = "fn invalid() { String::new().clear(); }",
         .code = "CV-ACCESS-NOT-ASSIGNABLE",
         .primary_text = "String::new().clear()"},
        {.name = "method arity",
         .source = "fn invalid() { var s = String::from_str(\"abc\"); s.append(); }",
         .code = "CV-TYPE-METHOD-CALL-ARITY",
         .primary_text = "s.append()"},
        {.name = "factory arity",
         .source = "fn invalid() { let s = String::new(\"x\"); }",
         .code = "CV-TYPE-METHOD-CALL-ARITY",
         .primary_text = "String::new(\"x\")"},
        {.name = "factory direct call only",
         .source = "fn invalid() { let factory = String::new; }",
         .code = "CV-TYPE-METHOD-CALL",
         .primary_text = "new"},
        {.name = "properties are not methods",
         .source = "fn invalid() { var s = String::from_str(\"abc\"); s.bytes(); }",
         .code = "CV-TYPE-METHOD-CALL",
         .primary_text = "bytes"},
        {.name = "container members stay private",
         .source = "fn invalid() { var s = String::from_str(\"abc\"); s.reserve(4usize); }",
         .code = "CV-TYPE-METHOD-CALL",
         .primary_text = "reserve"},
        {.name = "Read parameter rejects Write marker",
         .source = "fn invalid() { var s = String::from_str(\"abc\"); s.append(&s.as_str()); }",
         .code = "CV-ACCESS-CALL-MISMATCH",
         .primary_text = "&s.as_str()"},
        {.name = "Take is unavailable afterward",
         .source = "fn invalid() { var s = String::from_str(\"abc\"); let moved = &&s; s.len(); }",
         .code = "CV-ACCESS-UNAVAILABLE",
         .primary_text = "s.len()"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: String escapes failures and foreign boundaries") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "local return",
         .source = "fn bad() -> str { let s = String::new(); return s.as_str(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.as_str()"},
        {.name = "Take return",
         .source = "fn bad(&&s: String) -> str { return s.as_str(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.as_str()"},
        {.name = "local throw",
         .source =
             "struct E { text: str } fn bad() throw E { let s = String::new(); throw E { text: s.as_str() }; }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.as_str()"},
        {.name = "local protected throw",
         .source =
             "struct E { text: str } fn bad() { try { let s = String::new(); throw E { text: s.as_str() }; } catch { E(e) => { let _ = e.text.len(); }, } }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.as_str()"},
        {.name = "caught original holder",
         .source =
             "struct E { text: str } fn bad() { var s = String::new(); try { throw E { text: s.as_str() }; } catch { E(_) => { s.clear(); }, } }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "guard original holder",
         .source =
             "struct E { text: str } fn modify(&s: String) -> bool { s.clear(); return false; } fn bad() { var s = String::new(); try { throw E { text: s.as_str() }; } catch { E(_) if modify(&s) => {}, E(_) => {}, } }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "handler local return",
         .source =
             "struct E {} fn bad() -> str { return try { throw E {}; \"\" } catch { E(_) => { let s = String::new(); s.as_str() }, }; }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.as_str()"},
        {.name = "Write output local",
         .source = "fn bad(&v: str) { let s = String::new(); v = s.as_str(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "v = s.as_str()"},
        {.name = "comparison pending view",
         .source =
             "fn mutate(&s: String) -> str { s.clear(); return \"\"; } fn bad() { var s = String::new(); let b = s.as_str() == mutate(&s); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "receiver pending Take",
         .source =
             "fn consume(&&s: String) -> str => \"\"; fn bad() { var s = String::new(); s.append(consume(&&s)); }",
         .code = "CV-ACCESS-OPERATION-CONFLICT",
         .primary_text = "&&s"},
        {.name = "closure capture",
         .source =
             "fn bad() { var s = String::new(); let v = s.as_str(); let c = [v]() { return v.len(); }; s.clear(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "implicit construction",
         .source = "fn bad() { let s: String = \"a\"; }",
         .code = "CV-TYPE-MISMATCH",
         .primary_text = "\"a\""},
        {.name = "implicit borrowing",
         .source = "fn read(v: str) {} fn bad() { read(String::new()); }",
         .code = "CV-TYPE-MISMATCH",
         .primary_text = "String::new()"},
        {.name = "constant String",
         .source = "const s = String::new();",
         .code = "CV-CONST-INITIALIZER",
         .primary_text = "String::new()"},
        {.name = "direct throw String",
         .source = "fn bad() { throw String::new(); }",
         .code = "CV-EFFECT-THROW-TYPE",
         .primary_text = "throw String::new();"},
        {.name = "String as str",
         .source = "fn bad(s: String) { let v = s as str; }",
         .code = "CV-TYPE-CAST",
         .primary_text = "as"},
        {.name = "String construction",
         .source = "fn bad() { let s = String {}; }",
         .code = "CV-TYPE-CONSTRUCT-NOT-STRUCT",
         .primary_text = "String"},
        {.name = "unknown property",
         .source = "fn bad(s: String) { let v = s.capacity; }",
         .code = "CV-TYPE-TEXT-PROPERTY",
         .primary_text = "capacity"},
        {.name = "foreign preserves old slot",
         .source =
             "#[cpp] ---\ninline void replace(auto& v) noexcept { v = {}; }\n---\nfn bad() { var s = String::new(); var v = s.as_str(); ::replace(&v); s.clear(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "native pending view",
         .source =
             "#[cpp] ---\ninline void use(auto, auto) noexcept {}\n---\nfn bad() { var s = String::new(); ::use(s.as_str(), &&s); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: callable adaptation retains captured text loans") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "temporary closure passed as a callable view",
         .source = "fn make(v: str) => [v]() -> usize { return v.len(); }; "
                   "fn invoke(cb: fn() -> usize, &s: String) { s.clear(); let _ = cb(); } "
                   "fn invalid() { var s = String::new(); invoke(make(s.as_str()), &s); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.clear()"},
        {.name = "view into a closure-owned String prevents closure Take",
         .source = "fn make(s: String) => [s]() -> str { return s.as_str(); }; "
                   "fn invalid() { let closure = make(String::new()); let v = closure(); "
                   "let moved = &&closure; }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "&&"},
    });
    check_compiler_errors(cases);
}

TEST_CASE("Compiler diagnostics: String relationships survive joins and projected copies") {
    const auto cases = std::to_array<CompilerErrorExpectation>({
        {.name = "branch join retains either backing",
         .source =
             "fn invalid(choose: bool) { var first = String::new(); var second = String::new(); "
             "let view = if choose { first.as_str() } else { second.as_str() }; first.clear(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "first.clear()"},
        {.name = "loop join retains holder from an earlier iteration",
         .source = "fn invalid(again: bool) { var owner = String::new(); var view: str = \"\"; "
                   "while again { owner.clear(); view = owner.as_str(); } }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "owner.clear()"},
        {.name = "copying an owning field does not rebase another field's view",
         .source =
             "struct Mixed { owner: String, view: str } fn invalid() { var owner = String::new(); "
             "let mixed = Mixed { owner: owner, view: owner.as_str() }; let copy = mixed; owner.clear(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "owner.clear()"},
        {.name = "replacing one holder field leaves its sibling",
         .source =
             "struct Views { first: str, second: str } fn invalid() { var owner = String::new(); "
             "var views = Views { first: owner.as_str(), second: owner.as_str() }; "
             "views.first = \"\"; owner.clear(); }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "owner.clear()"},
        {.name = "failure from temporary Read backing cannot survive the call statement",
         .source =
             "struct E { view: str } fn fail(s: String) throw E { throw E { view: s.as_str() }; } "
             "fn invalid() { try { fail(String::new())?; } catch { E(_) => {}, } }",
         .code = "CV-ACCESS-BORROW-CONFLICT",
         .primary_text = "s.as_str()"},
    });
    check_compiler_errors(cases);
}
