module carven:test.internal.semantic.analysis.sequences;

import :diagnostics.code;
import :semantic.analysis.types;
import :semantic.semir.generic;
import :semantic.semir.program;
import :semantic.semir.type;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Sequence types: empty storage does not require an element default"_test = [] static noexcept {
        const auto program = analyze_test_program(R"(
            enum Tree { Leaf, Branch(Sequence<Tree>) }
            struct Holder<T> { items: Sequence<T> }
            fn empty() -> Sequence<Tree> => {};
            fn generic_empty() -> Holder<Tree> => {};
        )");
        auto owners = 0uz;
        for (const auto [id, type] : program.types().entries()) {
            static_cast<void>(id);
            if (std::holds_alternative<OwnedSequenceTypeValue>(type.value)) {
                ++owners;
            }
        }
        expect_equal(owners, 1uz);
        expect_equal(program.generic_nominal_instances().size(), 1uz);
    };

    "Sequence types: source constructors have one reserved name and one value argument"_test =
        [] static noexcept {
            expect(source_type_name_is_reserved("Sequence"));
            expect(source_type_name_is_reserved("ptr"));
            expect(source_type_name_is_reserved("range"));
            expect(!source_type_name_is_reserved("Holder"));
            expect_diagnostic(analyze_test_errors("struct Sequence {}"), DiagnosticCode::Catalog);
            expect_diagnostic(
                analyze_test_errors("struct Holder<Sequence> {}"),
                DiagnosticCode::TypeGenericDefinition
            );
            for (const auto text : {
                     "struct Box { value: Sequence }",
                     "struct Box { value: Sequence<i32, u32> }",
                     "struct Box<T> { value: Sequence<T, T> }",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::TypeUnresolved);
            }
            expect_diagnostic(
                analyze_test_errors("struct Box { value: Sequence<void> }"),
                DiagnosticCode::TypeValueRequired
            );
        };

    "Sequence calls: source access agrees with the selected intrinsic"_test = [] static noexcept {
        static_cast<void>(analyze_test_program(R"(
            fn accepted() {
                var values: Sequence<i32> = {};
                values.push(1);
                var item: i32 = 2;
                values.push(&&item);
                values[0] = 3;
                for &value in values { value += 1; }
                for value in values { let _ = value; }
                values.remove(0);
                values.clear();
                let _ = values.len();
                let _ = values.is_empty();
            }
        )"));
        static_cast<void>(analyze_test_program(R"(
            enum Group { Empty, Items(Sequence<i32>) }
            fn update(&group: Group) {
                match group {
                    .Empty => {},
                    .Items(&items) => {
                        for &item in items { item += 1; }
                    },
                }
            }
        )"));
        for (const auto text : {
                 "fn f() { let s: Sequence<i32> = {}; for &v in s { v += 1; } }",
                 R"(
                    enum Group { Empty, Items(Sequence<i32>) }
                    fn f(&group: Group) {
                        match group {
                            .Empty => {},
                            .Items(ref items) => { for &item in items { item += 1; } },
                        }
                    }
                )",
             }) {
            expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessRangeIterable);
        }
        for (const auto text : {
                 "fn f() { var s: Sequence<i32> = {}; var v = 1; s.push(&v); }",
                 "fn f() { var s: Sequence<i32> = {}; var index: usize = 0; s.remove(&&index); }",
             }) {
            expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessCallMismatch);
        }
        expect_diagnostic(
            analyze_test_errors(R"(
                struct Item { value: String }
                fn consume(&&value: String) {}
                fn rejected() {
                    var values: Sequence<Item> = {};
                    consume(&&values[0].value);
                }
            )"),
            DiagnosticCode::AccessTakeOperand
        );
        expect_diagnostic(
            analyze_test_errors("fn f() { var s: Sequence<i32> = {}; s.as_slice(); }"),
            DiagnosticCode::TypeMethodCall
        );
    };
});

} // namespace
