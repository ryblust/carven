module carven:test.internal.semantic.analysis.vector;

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
    "Vector types: empty storage does not require an element default"_test = [] static noexcept {
        const auto program = analyze_test_program(R"(
            enum Tree { Leaf, Branch(Vector<Tree>) }
            struct Holder<T> { items: Vector<T> }
            fn empty() -> Vector<Tree> => {};
            fn generic_empty() -> Holder<Tree> => {};
        )");
        static_cast<void>(program);
    };

    "Vector types: only the structural pointer name is reserved"_test = [] static noexcept {
        static_cast<void>(analyze_test_program("struct Vector {}"));
        static_cast<void>(analyze_test_program("struct Holder<Vector> { value: Vector }"));
        for (const auto text : {
                 "struct Box { value: Vector }",
                 "struct Box { value: Vector<i32, u32> }",
                 "struct Box<T> { value: Vector<T, T> }",
             }) {
            expect_diagnostic(analyze_test_errors(text), DiagnosticCode::TypeUnresolved);
        }
        static_cast<void>(analyze_test_program(R"(
                struct Box { value: Vector<void> }
                struct Holder<T> { value: Vector<(T, void)> }
                fn accepted(value: Holder<void>) -> Holder<void> => value;
                fn values() {
                    var units: Vector<void> = {};
                    units.push(());
                    var products: Vector<(i32, void)> = {};
                    products.push((1, ()));
                }
            )"));
        for (const auto text : {
                 "struct Box { value: Vector<(i32, str)> }",
                 "struct Box<T> { value: Vector<(T, str)> }",
             }) {
            expect_diagnostic(analyze_test_errors(text), DiagnosticCode::TypeVectorElement);
        }
    };

    "Vector calls: source access agrees with the selected intrinsic"_test = [] static noexcept {
        static_cast<void>(analyze_test_program(R"(
            fn accepted() {
                var values: Vector<i32> = {};
                values.push(1);
                var item: i32 = 2;
                values.push(&&item);
                values[0] = 3;
                for &value in values { value += 1; }
                for value in values { let _ = value; }
                values.remove(0);
                values.clear();
                let _ = values.length();
                let _ = values.is_empty();
            }
        )"));
        static_cast<void>(analyze_test_program(R"(
            enum Group { Empty, Items(Vector<i32>) }
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
                 "fn f() { let s: Vector<i32> = {}; for &v in s { v += 1; } }",
                 R"(
                    enum Group { Empty, Items(Vector<i32>) }
                    fn f(&group: Group) {
                        match group {
                            .Empty => {},
                            .Items(items) => { for &item in items { item += 1; } },
                        }
                    }
                )",
             }) {
            expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessRangeIterable);
        }
        for (const auto text : {
                 "fn f() { var s: Vector<i32> = {}; var v = 1; s.push(&v); }",
                 "fn f() { var s: Vector<i32> = {}; var index: usize = 0; s.remove(&&index); }",
             }) {
            expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessCallMismatch);
        }
        expect_diagnostic(
            analyze_test_errors(R"(
                struct Item { value: String }
                fn consume(&&value: String) {}
                fn rejected() {
                    var values: Vector<Item> = {};
                    consume(&&values[0].value);
                }
            )"),
            DiagnosticCode::AccessTakeOperand
        );
        static_cast<void>(
            analyze_test_program("fn f() { var s: Vector<i32> = {}; s.as_slice(); }")
        );
    };
});

} // namespace
