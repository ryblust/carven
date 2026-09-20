module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.classes;

import :diagnostics.code;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Classes: representation authority cannot be acquired through module calls") {
    const auto prelude = std::string(R"(
        class C {
            value: i32,
            fn create() -> C { return C { value: 1 }; }
            private fn secret(self) -> i32 { return self.value; }
            fn read(self) -> i32 { return self.secret(); }
            fn update(&self) { self.value += 1; }
        }
    )");
    const auto cases = std::array {
        "fn outside(c: C) -> i32 { return c.value; }",
        "fn outside() -> C { return C { value: 2 }; }",
        "fn outside() -> C { return C {}; }",
        "fn outside(c: C) -> i32 { return c.secret(); }",
        "class D { fn outside(c: C) -> i32 { return c.value; } }",
    };
    for (const auto source : cases) {
        CAPTURE(source);
        const auto diagnostics = analyze_test_errors(prelude + source);
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessClassPrivate));
    }
    const auto invalid_write = analyze_test_errors(prelude + "fn outside(c: C) { c.update(); }");
    CHECK(contains_diagnostic_code(invalid_write, DiagnosticCode::AccessImmutable));
}

TEST_CASE("Classes: default construction cannot bypass a factory through containers") {
    const auto prelude =
        std::string("class C { value: i32, fn create() -> C { return C { value: 1 }; } } ");
    const auto cases = std::array {
        "struct S { value: C } fn f() -> S { return S {}; }",
        "struct S { values: [C; 2] } fn f() -> S { return S {}; }",
        "class D { value: C, fn create() -> D { return D {}; } }",
    };
    for (const auto source : cases) {
        CAPTURE(source);
        const auto diagnostics = analyze_test_errors(prelude + source);
        CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::TypeDefaultInitialization));
    }
    const auto program =
        analyze_test_program(prelude + "struct S { values: [C; 0] } fn f() -> S { return S {}; }");
    CHECK(program.declarations().structures().size() == 2uz);
}

TEST_CASE("Classes: receivers and member names follow their declared contracts") {
    static_cast<void>(analyze_test_program(R"(
        class C {
            value: i32,
            fn create() -> C { return C { value: 1 }; }
            fn read(self) -> i32 { return self.value; }
            private fn update(&self) { self.value += 1; }
        }
    )"));
    static_cast<void>(analyze_test_program("class C { fn consume(&&self) {} }"));
    static_cast<void>(analyze_test_program("fn identity(self: i32) -> i32 { return self; }"));
    const auto unnamed = analyze_test_errors("class C { fn read(value) {} }");
    CHECK(contains_diagnostic_code(unnamed, DiagnosticCode::TypeParameterAnnotation));
    const auto explicit_receiver = analyze_test_errors("class C { fn read(self: C) {} }");
    CHECK(contains_diagnostic_code(explicit_receiver, DiagnosticCode::TypeParameterAnnotation));
    const auto duplicate = analyze_test_errors("class C { value: i32, fn value(self) {} }");
    CHECK(contains_diagnostic_code(duplicate, DiagnosticCode::Catalog));
}

TEST_CASE("Classes: runtime value support does not admit constant class execution") {
    const auto diagnostics =
        analyze_test_errors("class C {} const fn copy(value: C) -> C { return value; }");
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::ConstAdmission));
    const auto nested = analyze_test_errors(
        "class C {} struct S { value: C } const fn copy(value: S) -> S { return value; }"
    );
    CHECK(contains_diagnostic_code(nested, DiagnosticCode::ConstAdmission));
}

TEST_CASE("Classes: private storage remains visible to borrow checking") {
    const auto diagnostics = analyze_test_errors(R"(
        class Label {
            text: String,
            fn create() -> Label { return Label { text: String::from_str("first") }; }
            fn view(self) -> str { return self.text.as_str(); }
            fn replace(&self) { self.text = String::from_str("second"); }
        }
        fn use() {
            var label = Label::create();
            let view = label.view();
            label.replace();
            println(view);
        }
    )");
    CHECK(contains_diagnostic_code(diagnostics, DiagnosticCode::AccessBorrowConflict));
}

TEST_CASE("Classes: Take consumes the receiver and rejects escaping owned-field borrows") {
    const auto prelude = std::string(R"(
        class C {
            text: String,
            fn create() -> C { return C { text: String::from_str("x") }; }
            fn build(&&self) -> String { return (&&self).text; }
            fn view(self) -> str { return self.text.as_str(); }
        }
    )");
    const auto reused = analyze_test_errors(prelude + R"(
        fn use() { let owner = C::create(); let _ = owner.build(); let _ = owner.build(); }
    )");
    CHECK(contains_diagnostic_code(reused, DiagnosticCode::AccessUnavailable));
    const auto borrowed = analyze_test_errors(prelude + R"(
        fn use() {
            let owner = C::create();
            let view = owner.view();
            let _ = owner.build();
            println(view);
        }
    )");
    CHECK(contains_diagnostic_code(borrowed, DiagnosticCode::AccessBorrowConflict));
    const auto escaped = analyze_test_errors(R"(
        class C {
            text: String,
            fn bad(&&self) -> str { return (&&self).text.as_str(); }
        }
    )");
    CHECK(contains_diagnostic_code(escaped, DiagnosticCode::AccessBorrowConflict));
    const auto partial = analyze_test_errors(R"(
        class C {
            text: String,
            fn bad(&&self) -> String { return &&self.text; }
        }
    )");
    CHECK(contains_diagnostic_code(partial, DiagnosticCode::AccessTakeOperand));
}
