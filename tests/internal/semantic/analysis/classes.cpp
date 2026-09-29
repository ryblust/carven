module carven:test.internal.semantic.analysis.classes;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Semantic classes: representation authority cannot be acquired through module calls",
        [] static noexcept {
            const auto prelude = std::string(R"(
        class C {
            value: i32,
            fn create() -> C { return { value: 1 }; }
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
            ct::each(cases, std::identity {}, [&](const auto& source) noexcept {
                const auto diagnostics = analyze_test_errors(prelude + source);
                ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessClassPrivate);
            });
            const auto invalid_write =
                analyze_test_errors(prelude + "fn outside(c: C) { c.update(); }");
            ct::expect_diagnostic(invalid_write, DiagnosticCode::AccessImmutable);
        }
    );

    ct::test(
        "Semantic classes: default construction cannot bypass a factory through containers",
        [] static noexcept {
            const auto prelude =
                std::string("class C { value: i32, fn create() -> C { return { value: 1 }; } } ");
            const auto cases = std::array {
                "struct S { value: C } fn f() -> S { return S {}; }",
                "struct S { values: [C; 2] } fn f() -> S { return S {}; }",
                "class D { value: C, fn create() -> D { return D {}; } }",
            };
            ct::each(cases, std::identity {}, [&](const auto& source) noexcept {
                const auto diagnostics = analyze_test_errors(prelude + source);
                ct::expect_diagnostic(diagnostics, DiagnosticCode::TypeDefaultInitialization);
            });
            const auto program = analyze_test_program(
                prelude + "struct S { values: [C; 0] } fn f() -> S { return S {}; }"
            );
            ct::expect(program.declarations().structures().size() == 2uz);
        }
    );

    ct::test(
        "Semantic classes: receivers and member names follow their declared contracts",
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
        class C {
            value: i32,
            fn create() -> C { return { value: 1 }; }
            fn read(self) -> i32 { return self.value; }
            private fn update(&self) { self.value += 1; }
        }
    )"));
            static_cast<void>(analyze_test_program("class C { fn consume(&&self) {} }"));
            static_cast<void>(
                analyze_test_program("fn identity(self: i32) -> i32 { return self; }")
            );
            const auto unnamed = analyze_test_errors("class C { fn read(value) {} }");
            ct::expect_diagnostic(unnamed, DiagnosticCode::TypeParameterAnnotation);
            const auto explicit_receiver = analyze_test_errors("class C { fn read(self: C) {} }");
            ct::expect_diagnostic(explicit_receiver, DiagnosticCode::TypeParameterAnnotation);
            const auto duplicate = analyze_test_errors("class C { value: i32, fn value(self) {} }");
            ct::expect_diagnostic(duplicate, DiagnosticCode::Catalog);
        }
    );

    ct::test(
        "Semantic classes: ordinary function definitions have no constant execution gate",
        [] static noexcept {
            static_cast<void>(
                analyze_test_program("class C {} fn copy(value: C) -> C { return value; }")
            );
            static_cast<void>(analyze_test_program(
                "class C {} struct S { value: C } fn copy(value: S) -> S { return value; }"
            ));
        }
    );

    ct::test(
        "Semantic classes: private storage remains visible to borrow checking",
        [] static noexcept {
            const auto diagnostics = analyze_test_errors(R"(
        class Label {
            text: String,
            fn create() -> Label { return { text: String::from_str("first") }; }
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
            ct::expect_diagnostic(diagnostics, DiagnosticCode::AccessBorrowConflict);
        }
    );

    ct::test(
        "Semantic classes: Take consumes the receiver and rejects escaping owned-field borrows",
        [] static noexcept {
            const auto prelude = std::string(R"(
        class C {
            text: String,
            fn create() -> C { return { text: String::from_str("x") }; }
            fn build(&&self) -> String { return (&&self).text; }
            fn view(self) -> str { return self.text.as_str(); }
        }
    )");
            const auto reused = analyze_test_errors(prelude + R"(
        fn use() { let owner = C::create(); let _ = owner.build(); let _ = owner.build(); }
    )");
            ct::expect_diagnostic(reused, DiagnosticCode::AccessUnavailable);
            const auto borrowed = analyze_test_errors(prelude + R"(
        fn use() {
            let owner = C::create();
            let view = owner.view();
            let _ = owner.build();
            println(view);
        }
    )");
            ct::expect_diagnostic(borrowed, DiagnosticCode::AccessBorrowConflict);
            const auto escaped = analyze_test_errors(R"(
        class C {
            text: String,
            fn bad(&&self) -> str { return (&&self).text.as_str(); }
        }
    )");
            ct::expect_diagnostic(escaped, DiagnosticCode::AccessBorrowConflict);
            const auto partial = analyze_test_errors(R"(
        class C {
            text: String,
            fn bad(&&self) -> String { return &&self.text; }
        }
    )");
            ct::expect_diagnostic(partial, DiagnosticCode::AccessTakeOperand);
        }
    );
});

} // namespace
