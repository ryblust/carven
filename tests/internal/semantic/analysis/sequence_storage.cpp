module carven:test.internal.semantic.analysis.sequence_storage;

import :diagnostics.code;
import :diagnostics.sink;
import :semantic.analysis.diagnostics;
import :semantic.analysis.ownership.context;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Sequence storage: elements have modeled owned value semantics"_test = [] static noexcept {
        for (const auto text : {
                 "fn invalid(values: Sequence<str>) {}",
                 "fn invalid(values: Sequence<[u8]>) {}",
                 "fn invalid(values: Sequence<fn(i32) -> i32>) {}",
                 "import <string>; fn invalid(values: Sequence<::std::string>) {}",
                 "struct Borrowed { text: str } fn invalid(values: Sequence<Borrowed>) {}",
                 "struct Borrowed { text: str } struct Unused<T> { values: Sequence<Borrowed> }",
                 "struct Unused<T> { values: Sequence<str> }",
                 "struct Holder<T> { value: T } struct Unused<T> { values: Sequence<Holder<str>> }",
             }) {
            expect_diagnostic(analyze_test_errors(text), DiagnosticCode::TypeSequenceElement);
        }
        static_cast<void>(analyze_test_program(R"(
            struct Borrowed { text: str }
            struct Node<T> { value: T, children: Sequence<Node<T>> }
            fn use(values: Sequence<ptr<Borrowed>>, nodes: Node<String>) {}
            fn flags(values: Sequence<bool>, bytes: Sequence<u8x16>) {}
        )"));
    };

    "Sequence storage: value indirection does not admit direct layout cycles"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors("struct Node<T> { values: [Node<T>; 1] }"),
                DiagnosticCode::TypeRecursiveStorage
            );
            static_cast<void>(analyze_test_program("struct Node<T> { values: Sequence<Node<T>> }"));
        };

    "Sequence ownership: structural mutation protects views and iteration"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(fn invalid() {
                     var values = Sequence<String> {};
                     values.push("first" as String);
                     let view = values[0].as_str();
                     values.push("second" as String);
                     let observed = view.len();
                 })",
                     R"(struct Label { text: String } fn invalid() {
                     var values = Sequence<Label> {};
                     values.push(Label { text: "first" as String });
                     let view = values[0].text.as_str();
                     values.remove(0);
                     let observed = view.len();
                 })",
                     R"(fn invalid() {
                     var values = Sequence<i32> {};
                     values.push(1);
                     for item in values { values.clear(); }
                 })",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            expect_diagnostic(
                analyze_test_errors(R"(
            fn invalid() {
                var values = Sequence<i32> {};
                let moved = &&values;
                let observed = values.len();
            }
        )"),
                DiagnosticCode::AccessUnavailable
            );
            expect_diagnostic(
                analyze_test_errors(R"(
            fn invalid() {
                var values = Sequence<String> {};
                values.push("text" as String);
                let moved = &&values[0];
            }
        )"),
                DiagnosticCode::AccessTakeOperand
            );
            static_cast<void>(analyze_test_program(R"(
            fn accepted() {
                var values = Sequence<String> {};
                values.push("text" as String);
                var copy = values;
                let view = copy[0].as_str();
                values.clear();
                let observed = view.len();
            }
        )"));
        };

    "Sequence ownership: selected elements protect their carrier through consumers"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(fn invalid() {
                    var values = Sequence<i32> {};
                    values.push(1);
                    for item in values { values = Sequence<i32> {}; }
                })",
                     R"(fn reset(&values: Sequence<i32>) { values = Sequence<i32> {}; }
                fn invalid() {
                    var values = Sequence<i32> {};
                    values.push(1);
                    for &item in values { reset(&values); }
                })",
                     R"(struct Owner { values: Sequence<i32> }
                fn invalid() {
                    var owner = Owner { values: Sequence<i32> {} };
                    owner.values.push(1);
                    for item in owner.values { owner = Owner { values: Sequence<i32> {} }; }
                })",
                     R"(fn replacement(&values: Sequence<i32>) -> i32 {
                    values = Sequence<i32> {};
                    return 2;
                }
                fn invalid() {
                    var values = Sequence<i32> {};
                    values.push(1);
                    values[0] = replacement(&values);
                })",
                     R"(struct Element { value: i32 }
                fn replacement(&values: Sequence<Element>) -> i32 {
                    values.clear();
                    return 2;
                }
                fn invalid() {
                    var values = Sequence<Element> {};
                    values.push(Element { value: 1 });
                    values[0].value = replacement(&values);
                })",
                     R"(struct Element { value: i32 }
                fn replacement(&values: Sequence<Element>) -> i32 {
                    values[0] = Element { value: 2 };
                    return 3;
                }
                fn invalid() {
                    var values = Sequence<Element> {};
                    values.push(Element { value: 1 });
                    values[0].value = replacement(&values);
                })",
                     R"(fn replacement(&values: Sequence<i32>) -> usize {
                    values.clear();
                    return 0;
                }
                fn invalid() {
                    var values = Sequence<i32> {};
                    values.push(1);
                    values[replacement(&values)] = 2;
                })",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            static_cast<void>(analyze_test_program(R"(
                fn update(&values: Sequence<i32>) -> i32 {
                    values[0] = 2;
                    return 3;
                }
                fn append(&values: Sequence<i32>) { values.push(4); }
                struct Element { value: i32 }
                fn update_field(&values: Sequence<Element>) -> i32 {
                    values[0].value = 2;
                    return 3;
                }
                enum Node { Values(Sequence<i32>), Empty }
                fn update_node(&value: Node) {
                    match value {
                        .Values(&items) => { items.push(5); },
                        .Empty => {},
                    }
                }
                fn accepted() {
                    var values = Sequence<i32> {};
                    values.push(1);
                    values[0] = update(&values);
                    for &item in values { let ignored = update(&values); item = 4; }
                    append(&values);
                    var elements = Sequence<Element> {};
                    elements.push(Element { value: 1 });
                    elements[0].value = update_field(&elements);
                    var value: Node = .Values(Sequence<i32> {});
                    match value {
                        .Values(&items) => { update_node(&value); },
                        .Empty => {},
                    }
                }
            )"));
        };

    "Sequence ownership: copied arguments release element selection before later arguments"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                fn reset(&values: Sequence<i32>) -> i32 {
                    values = Sequence<i32> {};
                    return 2;
                }
                fn combine(first: i32, second: i32) -> i32 => first + second;
                fn accepted() {
                    var values = Sequence<i32> {};
                    values.push(1);
                    let result = combine(values[0], reset(&values));
                }
            )"));
            expect_diagnostic(
                analyze_test_errors(R"(
                fn reset(&values: Sequence<i32>) -> i32 {
                    values.clear();
                    return 2;
                }
                fn combine(&first: i32, second: i32) { first = second; }
                fn invalid() {
                    var values = Sequence<i32> {};
                    values.push(1);
                    combine(&values[0], reset(&values));
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Sequence ownership: borrowed array elements preserve their outer owner"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(struct Bucket { values: [i32; 2] }
                fn invalid() {
                    var buckets = Sequence<Bucket> {};
                    buckets.push(Bucket { values: [1, 2] });
                    for item in buckets[0].values { buckets.clear(); }
                })",
                     R"(fn reset_arrays(&values: Sequence<[i32; 1]>) -> i32 {
                    values.clear();
                    return 2;
                }
                fn combine_array(first: [i32; 1], second: i32) -> i32 => first[0] + second;
                fn invalid() {
                    var arrays = Sequence<[i32; 1]> {};
                    arrays.push([1]);
                    let result = combine_array(arrays[0], reset_arrays(&arrays));
                })",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            static_cast<void>(analyze_test_program(R"(
                struct Label { text: String }
                fn accepted() {
                    var strings = Sequence<String> {};
                    strings.push("first" as String);
                    strings.push(strings[0]);
                    var labels = Sequence<Label> {};
                    labels.push(Label { text: "first" as String });
                    labels.push(labels[0]);
                }
            )"));
        };

    "Borrowed payload ownership: projections pin ancestors and end with their arm"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
            enum Node { Values(Sequence<String>), Empty }
            fn invalid() {
                var value: Node = .Values(Sequence<String> {});
                match value {
                    .Values(&items) => { value = .Empty; },
                    .Empty => {},
                }
            }
        )"),
                DiagnosticCode::AccessBorrowConflict
            );
            static_cast<void>(analyze_test_program(R"(
            enum Node { Values(Sequence<String>), Empty }
            fn accepted() {
                var value: Node = .Values(Sequence<String> {});
                match value {
                    .Values(&items) => { items.push("first" as String); },
                    .Empty => {},
                }
                value = .Empty;
            }
        )"));
        };

    "Borrowed payload nullability: writes invalidate shared proofs"_test = [] static noexcept {
        expect_diagnostic(
            analyze_test_errors(R"(
            enum Slot { Value(ptr<i32>), Empty }
            fn invalid(pointer: ptr<i32>) {
                var slot: Slot = .Value(pointer);
                match slot {
                    .Value(ref outer) if outer != nullptr => {
                        match slot {
                            .Value(&inner) => { inner = nullptr; },
                            .Empty => {},
                        }
                        let observed = *outer;
                    },
                    _ => {},
                }
            }
        )"),
            DiagnosticCode::PointerNonNull
        );
        static_cast<void>(analyze_test_program(R"(
            enum Slot { Value(ptr<i32>), Empty }
            fn accepted(pointer: ptr<i32>) {
                var slot: Slot = .Value(pointer);
                match slot {
                    .Value(ref outer) if outer != nullptr => {
                        match slot {
                            .Value(&inner) => { inner = nullptr; },
                            .Empty => {},
                        }
                        if outer != nullptr { let observed = *outer; }
                    },
                    _ => {},
                }
            }
        )"));
    };

    "Borrowed payload nullability: binding another Read alias preserves observed facts"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                enum Slot { Value(ptr<i32>), Empty }
                fn accepted(pointer: ptr<i32>) {
                    var slot: Slot = .Value(pointer);
                    match slot {
                        .Value(ref outer) if outer != nullptr => {
                            match slot {
                                .Value(ref inner) => { let observed = *outer; },
                                .Empty => {},
                            }
                            let observed = *outer;
                        },
                        _ => {},
                    }
                }
            )"));
        };

    "Sequence ownership: recursive Read and Write referents have finite contracts"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                enum Node { Number(i32), Children(Sequence<Node>), Text(String) }
                fn count(node: Node) -> usize {
                    var result: usize = 1;
                    match node {
                        .Children(ref children) => {
                            for child in children { result += count(child); }
                        },
                        _ => {},
                    }
                    return result;
                }
                fn indexed_count(node: Node) -> usize {
                    var result: usize = 1;
                    match node {
                        .Children(ref children) => {
                            for index in 0usize..children.len() {
                                result += indexed_count(children[index]);
                            }
                        },
                        _ => {},
                    }
                    return result;
                }
                fn increment(&node: Node, &whole: Node) -> void {
                    match node {
                        .Number(&number) => { number += 1; },
                        .Children(&children) => {
                            for &child in children { increment(&child, &whole); }
                        },
                        _ => {},
                    }
                }
                fn first_text(node: Node) -> str {
                    match node {
                        .Text(ref text) => return text.as_str(),
                        .Children(ref children) => {
                            for child in children { return first_text(child); }
                        },
                        _ => {},
                    }
                    return "";
                }
                fn accepted() {
                    var root: Node = .Children(Sequence<Node> {});
                    increment(&root, &root);
                    let observed = count(root);
                    let indexed = indexed_count(root);
                    let view = first_text(root);
                    let length = view.len();
                }
            )"));
            expect_diagnostic(
                analyze_test_errors(R"(
                enum Node { Text(String), Children(Sequence<Node>), Empty }
                fn first_text(node: Node) -> str {
                    match node {
                        .Text(ref text) => return text.as_str(),
                        .Children(ref children) => {
                            for child in children { return first_text(child); }
                        },
                        .Empty => {},
                    }
                    return "";
                }
                fn invalid() {
                    var root: Node = .Children(Sequence<Node> {});
                    let view = first_text(root);
                    root = .Empty;
                    let observed = view.len();
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Sequence ownership: value indexing preserves its indirect carrier during index evaluation"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
                struct Bucket { values: [i32; 2] }
                fn clear_and_index(&buckets: Sequence<Bucket>) -> usize {
                    buckets.clear();
                    return 0;
                }
                fn invalid() {
                    var buckets = Sequence<Bucket> {};
                    buckets.push(Bucket { values: [1, 2] });
                    let observed = buckets[0].values[clear_and_index(&buckets)];
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Sequence ownership: calls preserve hidden readers and selected element identity"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(fn clear(&values: Sequence<String>) { values.clear(); }
                fn invalid() {
                    var values = Sequence<String> {};
                    values.push("first" as String);
                    let view = values[0].as_str();
                    clear(&values);
                    let observed = view.len();
                })",
                     R"(fn first(values: Sequence<String>) -> str => values[0].as_str();
                fn append(&value: String) { value.append("suffix"); }
                fn invalid() {
                    var values = Sequence<String> {};
                    values.push("first" as String);
                    let view = first(values);
                    append(&values[0]);
                    let observed = view.len();
                })",
                     R"(struct Label { text: String }
                fn replace(&element: Label, &whole: Sequence<Label>) {
                    let view = element.text.as_str();
                    whole[0] = Label { text: "replacement" as String };
                    let observed = view.len();
                }
                fn invalid() {
                    var values = Sequence<Label> {};
                    values.push(Label { text: "first" as String });
                    replace(&values[0], &values);
                })",
                     R"(struct Label { text: String }
                fn replace(&element: Label, &whole: Sequence<Label>, index: usize) {
                    let view = element.text.as_str();
                    whole[index] = Label { text: "replacement" as String };
                    let observed = view.len();
                }
                fn invalid(first: usize, second: usize) {
                    var values = Sequence<Label> {};
                    values.push(Label { text: "first" as String });
                    replace(&values[first], &values, second);
                })",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            static_cast<void>(analyze_test_program(R"(
                fn first(values: Sequence<String>) -> str => values[0].as_str();
                fn append(&value: String) { value.append("suffix"); }
                fn accepted() {
                    var values = Sequence<String> {};
                    values.push("first" as String);
                    values.push("second" as String);
                    let view = first(values);
                    values[1].append("suffix");
                    append(&values[1]);
                    let observed = view.len();
                }
            )"));
        };

    "Sequence ownership: element origins preserve aliasing and inline field separation"_test =
        [] static noexcept {
            for (const auto first : {"index", "0usize"}) {
                for (const auto second : {"index", "0usize"}) {
                    const auto source = std::format(
                        R"(
                        fn replace(first: String, &second: String) -> usize {{
                            let view = first.as_str();
                            second = "replacement" as String;
                            return view.len();
                        }}
                        fn invalid(index: usize) -> usize {{
                            var values = Sequence<String> {{}};
                            values.push("first" as String);
                            return replace(values[{}], &values[{}]);
                        }}
                    )",
                        first,
                        second
                    );
                    expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::AccessBorrowConflict
                    );
                    for (const auto whole : {false, true}) {
                        static_cast<void>(analyze_test_program(
                            std::format(
                                R"(
                            struct Pair {{ first: String, second: String }}
                            fn replace(first: String, &second: String{}) -> usize {{
                                let view = first.as_str();
                                second = "replacement" as String;
                                return view.len();
                            }}
                            fn accepted(index: usize) -> usize {{
                                var values = Sequence<Pair> {{}};
                                values.push(Pair {{ first: "first" as String, second: "second" as String }});
                                return replace(values[{}].first, &values[{}].second{});
                            }}
                        )",
                                whole ? ", whole: Sequence<Pair>" : "",
                                first,
                                second,
                                whole ? ", values" : ""
                            )
                        ));
                    }
                }
            }
            static_cast<void>(analyze_test_program(R"(
                fn replace(first: String, &second: String) -> usize {
                    let view = first.as_str();
                    second = "replacement" as String;
                    return view.len();
                }
                fn accepted(index: usize) -> usize {
                    var values = Sequence<String> {};
                    values.push("first" as String);
                    values.push("second" as String);
                    var copied = values;
                    let count = replace(values[0], &values[1]);
                    let independent = replace(values[index], &copied[index]);
                    return count;
                }
            )"));
        };

    "Sequence ownership: returned direct field loans retain their selected suffix"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                struct Pair { first: String, second: String }
                fn first(values: Sequence<Pair>, index: usize) -> str => values[index].first.as_str();
                fn accepted(index: usize) -> usize {
                    var values = Sequence<Pair> {};
                    values.push(Pair { first: "first" as String, second: "second" as String });
                    let view = first(values, index);
                    values[index].second = "replacement" as String;
                    return view.len();
                }
            )"));
            expect_diagnostic(
                analyze_test_errors(R"(
                struct Pair { first: String, second: String }
                fn first(values: Sequence<Pair>, index: usize) -> str => values[index].first.as_str();
                fn invalid(index: usize) -> usize {
                    var values = Sequence<Pair> {};
                    let view = first(values, index);
                    values[index].first = "replacement" as String;
                    return view.len();
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Sequence ownership: recursive carrier graphs have a bounded query domain"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            struct Node { text: String, other: String, children: Sequence<Node> }
            fn count(node: Node) -> usize {
                var result: usize = node.text.len();
                for child in node.children { result += count(child); }
                return result;
            }
            fn update(&node: Node) -> void {
                let view = node.text.as_str();
                for index in 0usize..node.children.len() { update(&node.children[index]); }
                node.other.append("suffix");
                let observed = view.len();
            }
            fn accepted(&node: Node) -> usize {
                update(&node);
                return count(node);
            }
        )");
            auto diagnostics = DiagnosticSink();
            const auto result =
                OwnershipBatchAnalyzer(program, AnalysisDiagnostics(diagnostics)).run();
            if (!expect(result.has_value())) {
                return;
            }
            // Three bodies and their finite selection sites bound the recursive graph;
            // runtime tree depth does not create additional allocation identities.
            expect(result->query_count <= 128uz);
            expect(result->evaluation_count <= 512uz);
            expect_diagnostic(
                analyze_test_errors(R"(
            struct Record { text: String, items: [String; 1] }
            fn invalid(&values: Sequence<Record>) {
                let taken = &&values[0].items[0];
            }
        )"),
                DiagnosticCode::AccessTakeOperand
            );
        };

    "Sequence execution: runtime ownership does not promise constant storage"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors("const fn invalid() -> Sequence<i32> => Sequence<i32> {};"),
                DiagnosticCode::ConstAdmission
            );
            expect_diagnostic(
                analyze_test_errors(R"(
            enum Number { Value(i32) }
            const fn invalid(value: Number) -> i32 {
                return match value { .Value(ref item) => item };
            }
        )"),
                DiagnosticCode::ConstAdmission
            );
        };
});

} // namespace
