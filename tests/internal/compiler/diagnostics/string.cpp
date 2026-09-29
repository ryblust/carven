module carven:test.internal.compiler.diagnostics.string;

import :diagnostics.code;
import :test.harness.framework;
import :test.internal.compiler.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Compiler diagnostics: String access and operation contracts", [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "named view blocks mutation",
             .source = "fn invalid() { var s: String = \"abc\"; let v: str = s; s.push('!'); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.push('!')"},
            {.name = "view copy keeps backing",
             .source =
                 "fn invalid() { var s: String = \"abc\"; var v = s.as_str(); let copy = v; v = \"\"; s.clear(); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "view blocks whole replacement",
             .source =
                 "fn invalid() { var s: String = \"abc\"; let v = s.as_str(); s = String {}; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s = String {}"},
            {.name = "view blocks Take",
             .source =
                 "fn invalid() { var s: String = \"abc\"; let v = s.as_str(); let taken = &&s; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "&&"},
            {.name = "self append is rejected",
             .source = "fn invalid() { var s: String = \"abc\"; s.append(s); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.append(s)"},
            {.name = "printing retains an earlier view while evaluating later arguments",
             .source =
                 "fn mutate(&s: String) -> str { s.clear(); return \"\"; } fn bad() { var s: String = \"abc\"; println(s.as_str(), mutate(&s)); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "pending view argument protects owner",
             .source =
                 "fn consume(v: str, &&s: String) {} fn invalid() { var s: String = \"abc\"; consume(s, &&s); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "&&s"},
            {.name = "temporary view cannot initialize holder",
             .source = "fn invalid() { let v: str = String::from_str(\"abc\"); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "let v: str = String::from_str(\"abc\")"},
            {.name = "stored range protects owner",
             .source = "fn invalid() { var s: String = \"abc\"; let range = s.chars; s.clear(); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "loop source protects owner",
             .source = "fn invalid() { var s: String = \"abc\"; for c in s.chars { s.clear(); } }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "aggregate view protects owner",
             .source =
                 "struct View { text: str } fn invalid() { var s: String = \"abc\"; let v = View { text: s }; s.clear(); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "unknown array index overlaps",
             .source =
                 "fn mutate(&values: [String; 2], index: usize) { values[index].clear(); } fn invalid() { var values = [String {}, String {}]; let v = values[0].as_str(); mutate(&values, 1); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "values[index].clear()"},
            {.name = "callee mutation checks caller holder",
             .source =
                 "fn mutate(&s: String) { s.clear(); } fn invalid() { var s: String = \"abc\"; let v = s.as_str(); mutate(&s); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "self reference aggregate",
             .source =
                 "struct Mixed { text: String, view: str } fn invalid() { var mixed = Mixed { text: String {}, view: \"\" }; mixed.view = mixed.text.as_str(); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "mixed.view = mixed.text.as_str()"},
            {.name = "immutable receiver",
             .source = "fn invalid() { let s = String {}; s.clear(); }",
             .code = DiagnosticCode::AccessImmutable,
             .primary_text = "s.clear()"},
            {.name = "temporary receiver cannot write",
             .source = "fn invalid() { String {}.clear(); }",
             .code = DiagnosticCode::AccessNotAssignable,
             .primary_text = "String {}.clear()"},
            {.name = "method arity",
             .source = "fn invalid() { var s: String = \"abc\"; s.append(); }",
             .code = DiagnosticCode::TypeMethodCallArity,
             .primary_text = "s.append()"},
            {.name = "factory arity",
             .source = "fn invalid() { let s = String::from_str(); }",
             .code = DiagnosticCode::TypeMethodCallArity,
             .primary_text = "String::from_str()"},
            {.name = "factory direct call only",
             .source = "fn invalid() { let factory = String::from_str; }",
             .code = DiagnosticCode::TypeMethodCall,
             .primary_text = "from_str"},
            {.name = "properties are not methods",
             .source = "fn invalid() { var s: String = \"abc\"; s.bytes(); }",
             .code = DiagnosticCode::TypeMethodCall,
             .primary_text = "bytes"},
            {.name = "container members stay private",
             .source = "fn invalid() { var s: String = \"abc\"; s.reserve(4usize); }",
             .code = DiagnosticCode::TypeMethodCall,
             .primary_text = "reserve"},
            {.name = "Read parameter rejects Write marker",
             .source = "fn invalid() { var s: String = \"abc\"; s.append(&s.as_str()); }",
             .code = DiagnosticCode::AccessCallMismatch,
             .primary_text = "&s.as_str()"},
            {.name = "Take is unavailable afterward",
             .source = "fn invalid() { var s: String = \"abc\"; let moved = &&s; s.len(); }",
             .code = DiagnosticCode::AccessUnavailable,
             .primary_text = "s.len()"},
        });
        check_compiler_errors(cases);
    });

    ct::test("Compiler diagnostics: String escapes failures and foreign boundaries", [] static noexcept {
        const auto cases = std::to_array<CompilerErrorExpectation>({
            {.name = "local return",
             .source = "fn bad() -> str { let s = String {}; return s; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s"},
            {.name = "Take return",
             .source = "fn bad(&&s: String) -> str { return s; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s"},
            {.name = "local throw",
             .source =
                 "struct E { text: str } fn bad() throw E { let s = String {}; throw E { text: s }; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s"},
            {.name = "local protected throw",
             .source =
                 "struct E { text: str } fn bad() { try { let s = String {}; throw E { text: s }; } catch { E(e) => { let _ = e.text.len(); }, } }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s"},
            {.name = "caught original holder",
             .source =
                 "struct E { text: str } fn bad() { var s = String {}; try { throw E { text: s }; } catch { E(_) => s.clear(), } }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "guard original holder",
             .source =
                 "struct E { text: str } fn modify(&s: String) -> bool { s.clear(); return false; } fn bad() { var s = String {}; try { throw E { text: s }; } catch { E(_) if modify(&s) => {}, E(_) => {}, } }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "handler local return",
             .source =
                 "struct E {} fn bad() -> str { return try { throw E {}; \"\" } catch { E(_) => { let s = String {}; s }, }; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "{ let s = String {}; s }"},
            {.name = "Write output local",
             .source = "fn bad(&v: str) { let s = String {}; v = s; }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "v = s"},
            {.name = "comparison pending view",
             .source =
                 "fn mutate(&s: String) -> str { s.clear(); return \"\"; } fn bad() { var s = String {}; let b = s.as_str() == mutate(&s); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "receiver pending Take",
             .source =
                 "fn consume(&&s: String) -> str => \"\"; fn bad() { var s = String {}; s.append(consume(&&s)); }",
             .code = DiagnosticCode::AccessOperationConflict,
             .primary_text = "&&s"},
            {.name = "closure capture",
             .source =
                 "fn bad() { var s = String {}; let v = s.as_str(); let c = [v]() { return v.len(); }; s.clear(); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "str values require explicit owning conversion",
             .source = "fn bad(value: str) { let s: String = value; }",
             .code = DiagnosticCode::TypeMismatch,
             .primary_text = "value"},
            {.name = "direct throw String",
             .source = "fn bad() { throw String {}; }",
             .code = DiagnosticCode::EffectThrowType,
             .primary_text = "throw String {};"},
            {.name = "String as str",
             .source = "fn bad(s: String) { let v = s as str; }",
             .code = DiagnosticCode::TypeCast,
             .primary_text = "as"},
            {.name = "nonempty String structure construction",
             .source = "fn bad() { let s = String { 1 }; }",
             .code = DiagnosticCode::TypeConstructNotStruct,
             .primary_text = "String"},
            {.name = "unknown property",
             .source = "fn bad(s: String) { let v = s.capacity; }",
             .code = DiagnosticCode::TypeTextProperty,
             .primary_text = "capacity"},
            {.name = "foreign preserves old slot",
             .source =
                 "#[cpp] ---\ninline void replace(auto& v) noexcept { v = {}; }\n---\nfn bad() { var s = String {}; var v = s.as_str(); ::replace(&v); s.clear(); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s.clear()"},
            {.name = "native pending view",
             .source =
                 "#[cpp] ---\ninline void use(auto, auto) noexcept {}\n---\nfn bad() { var s = String {}; ::use(s.as_str(), &&s); }",
             .code = DiagnosticCode::AccessBorrowConflict,
             .primary_text = "s"},
        });
        check_compiler_errors(cases);
    });

    ct::test(
        "Compiler diagnostics: callable adaptation retains captured text loans",
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "temporary closure passed as a callable view",
                 .source = "fn make(v: str) => [v]() -> usize { return v.len(); }; "
                           "fn invoke(cb: fn() -> usize, &s: String) { s.clear(); let _ = cb(); } "
                           "fn invalid() { var s = String {}; invoke(make(s.as_str()), &s); }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "s.clear()"},
                {.name = "view into a closure-owned String prevents closure Take",
                 .source = "fn make(s: String) => [s]() -> str { return s; }; "
                           "fn invalid() { let closure = make(String {}); let v = closure(); "
                           "let moved = &&closure; }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "&&"},
            });
            check_compiler_errors(cases);
        }
    );

    ct::test(
        "Compiler diagnostics: String relationships survive joins and projected copies",
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "branch join retains either backing",
                 .source =
                     "fn invalid(choose: bool) { var first = String {}; var second = String {}; "
                     "let view: str = if choose { first } else { second }; first.clear(); }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "first.clear()"},
                {.name = "loop join retains holder from an earlier iteration",
                 .source = "fn invalid(again: bool) { var owner = String {}; var view: str = \"\"; "
                           "while again { owner.clear(); view = owner; } }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "owner.clear()"},
                {.name = "copying an owning field does not rebase another field's view",
                 .source =
                     "struct Mixed { owner: String, view: str } fn invalid() { var owner = String {}; "
                     "let mixed = Mixed { owner: owner, view: owner.as_str() }; let copy = mixed; owner.clear(); }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "owner.clear()"},
                {.name = "replacing one holder field leaves its sibling",
                 .source =
                     "struct Views { first: str, second: str } fn invalid() { var owner = String {}; "
                     "var views = Views { first: owner.as_str(), second: owner.as_str() }; "
                     "views.first = \"\"; owner.clear(); }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "owner.clear()"},
                {.name = "failure from temporary Read backing cannot survive the call statement",
                 .source =
                     "struct E { view: str } fn fail(s: String) throw E { throw E { view: s }; } "
                     "fn invalid() { try { fail(String {})?; } catch { E(_) => {}, } }",
                 .code = DiagnosticCode::AccessBorrowConflict,
                 .primary_text = "s"},
            });
            check_compiler_errors(cases);
        }
    );

    ct::test(
        "Compiler diagnostics: multiline errors retain original source locations",
        [] static noexcept {
            const auto cases = std::to_array<CompilerErrorExpectation>({
                {.name = "escape after indentation",
                 .source = "fn invalid() { let s = \"\"\"\n    \\q\n\"\"\"; }",
                 .code = DiagnosticCode::Lexical,
                 .primary_text = "\\"},
                {.name = "inline closing delimiter",
                 .source = "fn invalid() { let s = r\"\"\"\n    text\"\"\"; }",
                 .code = DiagnosticCode::Lexical,
                 .primary_text = "\"\"\""},
                {.name = "interpolated inline closing delimiter",
                 .source = "fn invalid() { let s = f\"\"\"\n    {0}\"\"\"; }",
                 .code = DiagnosticCode::Lexical,
                 .primary_text = "\"\"\""},
            });
            check_compiler_errors(cases);
        }
    );
});

} // namespace
