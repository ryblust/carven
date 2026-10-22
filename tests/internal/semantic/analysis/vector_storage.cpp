module carven:test.internal.semantic.analysis.vector_storage;

import :diagnostics.code;
import :diagnostics.sink;
import :semantic.analysis.diagnostics;
import :semantic.analysis.generics.snapshot;
import :semantic.analysis.model;
import :semantic.analysis.ownership.context;
import :semantic.semir.program;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto recursive_child_whole_borrows(bool may_fail) noexcept -> std::string {
    auto source = std::string(
        "struct Node { text: String, other: String, left: Vector<Node>, right: Vector<Node> }\n"
    );
    if (may_fail) {
        source += "struct ViewError { text: str }\n";
    }
    for (auto index = 0uz; index < 3uz; ++index) {
        source += std::format(
            R"(
            fn f{}(node: Node, whole: Node, depth: usize, choose: bool{}) -> str{} {{
                if depth == 0 {{
                    {}
                    return node.text.as_str();
                }}
                if choose {{ return f{}(node.left[0], whole, depth - 1, choose{}){}; }}
                return f{}(node.right[0], whole, depth - 1, choose{}){};
            }}
        )",
            index,
            may_fail ? ", fail: bool" : "",
            may_fail ? " throw ViewError" : "",
            may_fail ? "if fail { throw ViewError { text: node.text.as_str() }; }" : "",
            (index + 1uz) % 3uz,
            may_fail ? ", fail" : "",
            may_fail ? "?" : "",
            (index + 1uz) % 3uz,
            may_fail ? ", fail" : "",
            may_fail ? "?" : ""
        );
    }
    return source;
}

auto expect_bounded_ownership(const SemIRProgram& program, std::size_t source_size) noexcept
    -> void {
    auto diagnostics = DiagnosticSink();
    const auto generics = GenericAnalysisSnapshot {
        .owner = std::nullopt,
        .definitions = {},
        .types = {},
        .bodies = {},
        .records = {},
        .enum_payloads = {},
        .failures = {},
    };
    const auto model = AnalysisModelView(program, generics);
    const auto summary = OwnershipBatchAnalyzer(model, AnalysisDiagnostics(diagnostics)).run();
    if (!expect(summary.has_value())) {
        return;
    }
    // Source bodies and selection sites bound the abstract problem, independently
    // of runtime nesting and the solver's choice of representation.
    expect(summary->query_count <= 128uz * source_size);
    expect(summary->evaluation_count <= 512uz * source_size);
    expect(summary->storage_node_count <= 2048uz * source_size);
    expect(summary->storage_edge_count <= 2048uz * source_size);
    expect(diagnostics.empty());
}

const TestSuite suite([] static noexcept {
    "Vector storage: elements retain source and native ownership contracts"_test =
        [] static noexcept {
            for (
                const auto text : {
                    "fn invalid(values: Vector<str>) {}",
                    "fn invalid(values: Vector<[u8]>) {}",
                    "fn invalid(values: Vector<fn(i32) -> i32>) {}",
                    "struct Borrowed { text: str } fn invalid(values: Vector<Borrowed>) {}",
                    "struct Borrowed { text: str } struct Unused<T> { values: Vector<Borrowed> }",
                    "struct Unused<T> { values: Vector<str> }",
                    "struct Holder<T> { value: T } struct Unused<T> { values: Vector<Holder<str>> }",
                }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::TypeVectorElement);
            }
            static_cast<void>(analyze_test_program(R"(
            import <string>;
            struct Native { text: ::std::string }
            struct NativeRows<T> { rows: Vector<T> }
            fn native(values: Vector<::std::string>, records: NativeRows<Native>) {}
            struct Borrowed { text: str }
            struct Node<T> { value: T, children: Vector<Node<T>> }
            fn use(values: Vector<ptr<Borrowed>>, nodes: Node<String>) {}
            fn flags(values: Vector<bool>, bytes: Vector<u8x16>) {}
        )"));
        };

    "Vector storage: value indirection does not admit direct layout cycles"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors("struct Node<T> { values: [Node<T>; 1] }"),
                DiagnosticCode::TypeRecursiveStorage
            );
            static_cast<void>(analyze_test_program("struct Node<T> { values: Vector<Node<T>> }"));
        };

    "Vector ownership: recursive mutation follows a completed scalar lookup"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                struct Item { name: String, children: Vector<Item> }
                fn locate(items: Vector<Item>, name: str) -> usize {
                    for index in 0..items.length() {
                        if items[index].name.as_str() == name { return index; }
                    }
                    return items.length();
                }
                fn insert(&item: Item, remaining: usize) -> void {
                    let index = locate(item.children, "child");
                    if index == item.children.length() {
                        item.children.push({ name: "child", children: [] });
                    }
                    if remaining != 0 { insert(&item.children[index], remaining - 1); }
                }
                fn valid() {
                    var root = Item { name: "root", children: [] };
                    insert(&root, 2);
                }
            )"));
        };

    "Vector ownership: structural mutation protects views and iteration"_test = [] static noexcept {
        for (const auto text : {
                 R"(fn invalid() {
                     var values = Vector<String> {};
                     values.push("first" as String);
                     let view = values[0].as_str();
                     values.push("second" as String);
                     let observed = view.byte_length();
                 })",
                 R"(struct Label { text: String } fn invalid() {
                     var values = Vector<Label> {};
                     values.push(Label { text: "first" as String });
                     let view = values[0].text.as_str();
                     values.remove(0);
                     let observed = view.byte_length();
                 })",
                 R"(fn invalid() {
                     var values = Vector<i32> {};
                     values.push(1);
                     for item in values { values.clear(); }
                 })",
                 R"(struct Node { text: String, children: Vector<Node> }
                 fn children(node: Node) -> [Node] => node.children;
                 fn invalid(&root: Node) -> usize {
                     let view = children(root)[0].text.as_str();
                     root.children[0].text.clear();
                     return view.byte_length();
                 })",
             }) {
            expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
        }
        expect_diagnostic(
            analyze_test_errors(R"(
            fn invalid() {
                var values = Vector<i32> {};
                let moved = &&values;
                let observed = values.length();
            }
        )"),
            DiagnosticCode::AccessUnavailable
        );
        expect_diagnostic(
            analyze_test_errors(R"(
            fn invalid() {
                var values = Vector<String> {};
                values.push("text" as String);
                let moved = &&values[0];
            }
        )"),
            DiagnosticCode::AccessTakeOperand
        );
        static_cast<void>(analyze_test_program(R"(
            fn accepted() {
                var values = Vector<String> {};
                values.push("text" as String);
                var copy = values;
                let view = copy[0].as_str();
                values.clear();
                let observed = view.byte_length();
            }
        )"));
    };

    "Vector ownership: selected elements protect their carrier through consumers"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(fn invalid() {
                    var values = Vector<i32> {};
                    values.push(1);
                    for item in values { values = Vector<i32> {}; }
                })",
                     R"(fn reset(&values: Vector<i32>) { values = Vector<i32> {}; }
                fn invalid() {
                    var values = Vector<i32> {};
                    values.push(1);
                    for &item in values { reset(&values); }
                })",
                     R"(struct Owner { values: Vector<i32> }
                fn invalid() {
                    var owner = Owner { values: Vector<i32> {} };
                    owner.values.push(1);
                    for item in owner.values { owner = Owner { values: Vector<i32> {} }; }
                })",
                     R"(fn replacement(&values: Vector<i32>) -> i32 {
                    values = Vector<i32> {};
                    return 2;
                }
                fn invalid() {
                    var values = Vector<i32> {};
                    values.push(1);
                    values[0] = replacement(&values);
                })",
                     R"(struct Element { value: i32 }
                fn replacement(&values: Vector<Element>) -> i32 {
                    values.clear();
                    return 2;
                }
                fn invalid() {
                    var values = Vector<Element> {};
                    values.push(Element { value: 1 });
                    values[0].value = replacement(&values);
                })",
                     R"(struct Element { value: i32 }
                fn replacement(&values: Vector<Element>) -> i32 {
                    values[0] = Element { value: 2 };
                    return 3;
                }
                fn invalid() {
                    var values = Vector<Element> {};
                    values.push(Element { value: 1 });
                    values[0].value = replacement(&values);
                })",
                     R"(fn replacement(&values: Vector<i32>) -> usize {
                    values.clear();
                    return 0;
                }
                fn invalid() {
                    var values = Vector<i32> {};
                    values.push(1);
                    values[replacement(&values)] = 2;
                })",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            static_cast<void>(analyze_test_program(R"(
                fn update(&values: Vector<i32>) -> i32 {
                    values[0] = 2;
                    return 3;
                }
                fn append(&values: Vector<i32>) { values.push(4); }
                struct Element { value: i32 }
                fn update_field(&values: Vector<Element>) -> i32 {
                    values[0].value = 2;
                    return 3;
                }
                enum Node { Values(Vector<i32>), Empty }
                fn update_node(&value: Node) {
                    match value {
                        .Values(&items) => { items.push(5); },
                        .Empty => {},
                    }
                }
                fn accepted() {
                    var values = Vector<i32> {};
                    values.push(1);
                    values[0] = update(&values);
                    for &item in values { let ignored = update(&values); item = 4; }
                    append(&values);
                    var elements = Vector<Element> {};
                    elements.push(Element { value: 1 });
                    elements[0].value = update_field(&elements);
                    var value: Node = .Values(Vector<i32> {});
                    match value {
                        .Values(&items) => { update_node(&value); },
                        .Empty => {},
                    }
                }
            )"));
        };

    "Vector ownership: copied arguments release element selection before later arguments"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                fn reset(&values: Vector<i32>) -> i32 {
                    values = Vector<i32> {};
                    return 2;
                }
                fn combine(first: i32, second: i32) -> i32 => first + second;
                fn accepted() {
                    var values = Vector<i32> {};
                    values.push(1);
                    let result = combine(values[0], reset(&values));
                }
            )"));
            expect_diagnostic(
                analyze_test_errors(R"(
                fn reset(&values: Vector<i32>) -> i32 {
                    values.clear();
                    return 2;
                }
                fn combine(&first: i32, second: i32) { first = second; }
                fn invalid() {
                    var values = Vector<i32> {};
                    values.push(1);
                    combine(&values[0], reset(&values));
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Vector ownership: borrowed array elements preserve their outer owner"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(struct Bucket { values: [i32; 2] }
                fn invalid() {
                    var buckets = Vector<Bucket> {};
                    buckets.push(Bucket { values: [1, 2] });
                    for item in buckets[0].values { buckets.clear(); }
                })",
                     R"(fn reset_arrays(&values: Vector<[i32; 1]>) -> i32 {
                    values.clear();
                    return 2;
                }
                fn combine_array(first: [i32; 1], second: i32) -> i32 => first[0] + second;
                fn invalid() {
                    var arrays = Vector<[i32; 1]> {};
                    arrays.push([1]);
                    let result = combine_array(arrays[0], reset_arrays(&arrays));
                })",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            static_cast<void>(analyze_test_program(R"(
                struct Label { text: String }
                fn accepted() {
                    var strings = Vector<String> {};
                    strings.push("first" as String);
                    strings.push(strings[0]);
                    var labels = Vector<Label> {};
                    labels.push(Label { text: "first" as String });
                    labels.push(labels[0]);
                }
            )"));
        };

    "Vector ownership: recursive Read and Write referents have finite contracts"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                enum Node { Number(i32), Children(Vector<Node>), Text(String) }
                fn count(node: Node) -> usize {
                    var result: usize = 1;
                    match node {
                        .Children(children) => {
                            for child in children { result += count(child); }
                        },
                        _ => {},
                    }
                    return result;
                }
                fn indexed_count(node: Node) -> usize {
                    var result: usize = 1;
                    match node {
                        .Children(children) => {
                            for index in 0usize..children.length() {
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
                        .Text(text) => return text.as_str(),
                        .Children(children) => {
                            for child in children { return first_text(child); }
                        },
                        _ => {},
                    }
                    return "";
                }
                fn accepted() {
                    var root: Node = .Children(Vector<Node> {});
                    increment(&root, &root);
                    let observed = count(root);
                    let indexed = indexed_count(root);
                    let view = first_text(root);
                    let length = view.byte_length();
                }
            )"));
            expect_diagnostic(
                analyze_test_errors(R"(
                enum Node { Text(String), Children(Vector<Node>), Empty }
                fn first_text(node: Node) -> str {
                    match node {
                        .Text(text) => return text.as_str(),
                        .Children(children) => {
                            for child in children { return first_text(child); }
                        },
                        .Empty => {},
                    }
                    return "";
                }
                fn invalid() {
                    var root: Node = .Children(Vector<Node> {});
                    let view = first_text(root);
                    root = .Empty;
                    let observed = view.byte_length();
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Vector ownership: value indexing preserves its indirect carrier during index evaluation"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
                struct Bucket { values: [i32; 2] }
                fn clear_and_index(&buckets: Vector<Bucket>) -> usize {
                    buckets.clear();
                    return 0;
                }
                fn invalid() {
                    var buckets = Vector<Bucket> {};
                    buckets.push(Bucket { values: [1, 2] });
                    let observed = buckets[0].values[clear_and_index(&buckets)];
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Vector ownership: calls preserve hidden readers and selected element identity"_test =
        [] static noexcept {
            for (const auto text : {
                     R"(fn clear(&values: Vector<String>) { values.clear(); }
                fn invalid() {
                    var values = Vector<String> {};
                    values.push("first" as String);
                    let view = values[0].as_str();
                    clear(&values);
                    let observed = view.byte_length();
                })",
                     R"(fn first(values: Vector<String>) -> str => values[0].as_str();
                fn append(&value: String) { value.append("suffix"); }
                fn invalid() {
                    var values = Vector<String> {};
                    values.push("first" as String);
                    let view = first(values);
                    append(&values[0]);
                    let observed = view.byte_length();
                })",
                     R"(struct Label { text: String }
                fn replace(&element: Label, &whole: Vector<Label>) {
                    let view = element.text.as_str();
                    whole[0] = Label { text: "replacement" as String };
                    let observed = view.byte_length();
                }
                fn invalid() {
                    var values = Vector<Label> {};
                    values.push(Label { text: "first" as String });
                    replace(&values[0], &values);
                })",
                     R"(struct Label { text: String }
                fn replace(&element: Label, &whole: Vector<Label>, index: usize) {
                    let view = element.text.as_str();
                    whole[index] = Label { text: "replacement" as String };
                    let observed = view.byte_length();
                }
                fn invalid(first: usize, second: usize) {
                    var values = Vector<Label> {};
                    values.push(Label { text: "first" as String });
                    replace(&values[first], &values, second);
                })",
                 }) {
                expect_diagnostic(analyze_test_errors(text), DiagnosticCode::AccessBorrowConflict);
            }
            static_cast<void>(analyze_test_program(R"(
                fn first(values: Vector<String>) -> str => values[0].as_str();
                fn append(&value: String) { value.append("suffix"); }
                fn accepted() {
                    var values = Vector<String> {};
                    values.push("first" as String);
                    values.push("second" as String);
                    let view = first(values);
                    values[1].append("suffix");
                    append(&values[1]);
                    let observed = view.byte_length();
                }
            )"));
        };

    "Vector ownership: element origins preserve aliasing and inline field separation"_test =
        [] static noexcept {
            for (const auto first : {"index", "0usize"}) {
                for (const auto second : {"index", "0usize"}) {
                    const auto source = std::format(
                        R"(
                        fn replace(first: String, &second: String) -> usize {{
                            let view = first.as_str();
                            second = "replacement" as String;
                            return view.byte_length();
                        }}
                        fn invalid(index: usize) -> usize {{
                            var values = Vector<String> {{}};
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
                                return view.byte_length();
                            }}
                            fn accepted(index: usize) -> usize {{
                                var values = Vector<Pair> {{}};
                                values.push(Pair {{ first: "first" as String, second: "second" as String }});
                                return replace(values[{}].first, &values[{}].second{});
                            }}
                        )",
                                whole ? ", whole: Vector<Pair>" : "",
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
                    return view.byte_length();
                }
                fn accepted(index: usize) -> usize {
                    var values = Vector<String> {};
                    values.push("first" as String);
                    values.push("second" as String);
                    var copied = values;
                    let count = replace(values[0], &values[1]);
                    let independent = replace(values[index], &copied[index]);
                    return count;
                }
            )"));
        };

    "Vector ownership: returned direct field loans retain their selected suffix"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                struct Pair { first: String, second: String }
                fn first(values: Vector<Pair>, index: usize) -> str => values[index].first.as_str();
                fn accepted(index: usize) -> usize {
                    var values = Vector<Pair> {};
                    values.push(Pair { first: "first" as String, second: "second" as String });
                    let view = first(values, index);
                    values[index].second = "replacement" as String;
                    return view.byte_length();
                }
            )"));
            expect_diagnostic(
                analyze_test_errors(R"(
                struct Pair { first: String, second: String }
                fn first(values: Vector<Pair>, index: usize) -> str => values[index].first.as_str();
                fn invalid(index: usize) -> usize {
                    var values = Vector<Pair> {};
                    let view = first(values, index);
                    values[index].first = "replacement" as String;
                    return view.byte_length();
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Vector ownership: recursive carrier graphs have a bounded query domain"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
            struct Node { text: String, other: String, children: Vector<Node> }
            fn count(node: Node) -> usize {
                var result: usize = node.text.byte_length();
                for child in node.children { result += count(child); }
                return result;
            }
            fn update(&node: Node) -> void {
                let view = node.text.as_str();
                for index in 0usize..node.children.length() { update(&node.children[index]); }
                node.other.append("suffix");
                let observed = view.byte_length();
            }
            fn accepted(&node: Node) -> usize {
                update(&node);
                return count(node);
            }
        )");
            expect_bounded_ownership(program, 3uz);
            expect_diagnostic(
                analyze_test_errors(R"(
            struct Record { text: String, items: [String; 1] }
            fn invalid(&values: Vector<Record>) {
                let taken = &&values[0].items[0];
            }
        )"),
                DiagnosticCode::AccessTakeOperand
            );
        };

    "Vector ownership: finite returned descendants preserve fields and siblings"_test = [] static noexcept {
        for (const auto depth : {1uz, 2uz, 4uz, 8uz}) {
            auto declarations = std::string("struct Leaf { text: String, other: i32 }\n");
            auto selected = std::string("root");
            for (auto index = 0uz; index < depth; ++index) {
                declarations += std::format(
                    "struct Layer{} {{ children: Vector<{}>, other: i32 }}\n",
                    index,
                    index == 0uz ? std::string("Leaf") : std::format("Layer{}", index - 1uz)
                );
                selected += ".children[0]";
            }
            const auto owner = std::format("Layer{}", depth - 1uz);
            declarations +=
                std::format("fn text(root: {}) -> str => {}.text.as_str();\n", owner, selected);
            for (const auto inline_read : {false, true}) {
                const auto read =
                    inline_read ? selected + ".text.as_str()" : std::string("text(root)");
                static_cast<void>(analyze_test_program(
                    declarations
                    + std::format(
                        "fn accepted(&root: {}) -> usize {{ let view = {}; {}.other = 42; root.children[1] = {} {{}}; return view.byte_length(); }}",
                        owner,
                        read,
                        selected,
                        depth == 1uz ? std::string("Leaf") : std::format("Layer{}", depth - 2uz)
                    )
                ));
                for (const auto& write :
                     {selected + ".text = \"changed\" as String;",
                      std::string("root.children.clear();")}) {
                    expect_diagnostic(
                        analyze_test_errors(
                            declarations
                            + std::format(
                                "fn invalid(&root: {}) -> usize {{ let view = {}; {} return view.byte_length(); }}",
                                owner,
                                read,
                                write
                            )
                        ),
                        DiagnosticCode::AccessBorrowConflict
                    );
                }
            }
        }
    };

    "Vector ownership: finite same-type selections and wrapper returns stay precise"_test =
        [] static noexcept {
            for (const auto depth : {1uz, 2uz, 4uz, 8uz}) {
                auto selected = std::string("root");
                for (auto index = 0uz; index < depth; ++index) {
                    selected += ".children[0]";
                }
                const auto prelude = std::format(
                    "struct Node {{ text: String, other: i32, children: Vector<Node> }}\n"
                    "fn text(root: Node) -> str => {}.text.as_str();\n"
                    "fn forward(root: Node) -> str => text(root);\n"
                    "fn wrapped(root: Node) -> str => forward(root);\n",
                    selected
                );
                for (const auto& read :
                     {selected + ".text.as_str()", std::string("wrapped(root)")}) {
                    static_cast<void>(analyze_test_program(
                        prelude
                        + std::format(
                            "fn accepted(&root: Node) -> usize {{ let view = {}; {}.other = 42; root.children[1].text.clear(); return view.byte_length(); }}",
                            read,
                            selected
                        )
                    ));
                    expect_diagnostic(
                        analyze_test_errors(
                            prelude
                            + std::format(
                                "fn invalid(&root: Node) -> usize {{ let view = {}; {}.text.clear(); return view.byte_length(); }}",
                                read,
                                selected
                            )
                        ),
                        DiagnosticCode::AccessBorrowConflict
                    );
                }
                static_cast<void>(analyze_test_program(prelude + R"(
                    fn siblings(&root: Node) -> usize {
                        let first = wrapped(root.children[0]);
                        let second = wrapped(root.children[1]);
                        root.children[0].other = 1;
                        root.children[1].other = 2;
                        return first.byte_length() + second.byte_length();
                    }
                )"));
            }
        };

    "Vector ownership: new descendant referents survive failure delivery and loop exits"_test =
        [] static noexcept {
            const auto prelude = std::string(R"(
                struct Node { text: String, other: i32, children: Vector<Node> }
                struct ViewError { text: str }
                fn nested(root: Node) -> str => root.children[0].children[0].text.as_str();
                fn maybe(root: Node, stop: bool) -> str throw ViewError {
                    let view = nested(root);
                    if stop { throw ViewError { text: view }; }
                    return view;
                }
            )");
            for (const auto same_field : {false, true}) {
                const auto write = same_field ? "root.children[0].children[0].text.clear();"
                                              : "root.children[0].children[0].other = 42;";
                const auto source = prelude
                    + std::format(R"(
                    fn probe(&root: Node, stop: bool) {{
                        try {{ let view = maybe(root, stop)?; {} let observed = view.byte_length(); }}
                        catch {{ ViewError(error) => {{ {} let observed = error.text.byte_length(); }}, }}
                    }}
                )",
                                  write,
                                  write);
                if (same_field) {
                    expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::AccessBorrowConflict
                    );
                } else {
                    static_cast<void>(analyze_test_program(source));
                }
                const auto loop = prelude
                    + std::format(R"(
                    fn loop_probe(&root: Node, depth: usize) -> usize {{
                        var view: str = "";
                        for index in 0usize..depth {{ view = nested(root); if index > 2 {{ break; }} continue; }}
                        {} return view.byte_length();
                    }}
                )",
                                  write);
                if (same_field) {
                    expect_diagnostic(
                        analyze_test_errors(loop),
                        DiagnosticCode::AccessBorrowConflict
                    );
                } else {
                    static_cast<void>(analyze_test_program(loop));
                }
            }
        };

    "Vector ownership: owned array feedback stabilizes graph queries and exits"_test = [] static noexcept {
        const auto prelude = std::string(
            "struct Node { children: Vector<[Node; 1]> }\n"
            "fn children(node: Node) -> [Node] => node.children[0];\n"
        );
        for (
            const auto control : {
                "for index in 0usize..depth { current = children(current[0]); }",
                "while depth > 0 { current = children(current[0]); depth -= 1; }",
                "for index in 0usize..depth { current = children(current[0]); if index > 2 { continue; } if index > 1 { break; } }",
                "for index in 0usize..depth { current = children(current[0]); if index > 2 { return current.length(); } }",
                "for outer in 0usize..depth { for inner in 0usize..depth { current = children(current[0]); } }",
            }) {
            const auto program = analyze_test_program(
                prelude
                + std::format(
                    "fn walk(root: [Node; 1], input: usize) -> usize {{ var depth = input; var current: [Node] = root; {} return current.length(); }}",
                    control
                )
            );
            expect_bounded_ownership(program, 3uz);
        }
        for (
            const auto invalid : {
                "fn invalid() -> [Node] { var local: [Node; 1] = [Node {}]; return children(local[0]); }",
                "fn invalid(&root: [Node; 1]) -> usize { let view = children(root[0]); root[0].children.clear(); return view.length(); }",
            }) {
            expect_diagnostic(
                analyze_test_errors(prelude + invalid),
                DiagnosticCode::AccessBorrowConflict
            );
        }
    };

    "Vector ownership: recursive descendant returns share a finite graph domain"_test = [] static noexcept {
        for (
            const auto recursive : {
                "fn walk(node: Node, depth: usize) -> [Node] { if depth == 0 { return node.children[0]; } return walk(node.children[0][0], depth - 1); }",
                "fn walk(node: Node, depth: usize) -> [Node] { if depth == 0 { return node.children[0]; } return other(node.children[0][0], depth - 1); } fn other(node: Node, depth: usize) -> [Node] => walk(node, depth);",
            }) {
            const auto program = analyze_test_program(
                std::string("struct Node { children: Vector<[Node; 1]> }\n") + recursive
            );
            expect_bounded_ownership(program, 2uz);
        }
    };

    "Vector ownership: recursive mixed roots retain local escape constraints"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
            struct Node { text: String, children: Vector<[Node; 1]> }
            fn cross(external: [Node; 1], depth: usize) -> str {
                if depth == 0 { return ""; }
                var local: [Node; 1] = [Node {}];
                var current: [Node] = external;
                if depth > 1 { current = local; }
                let observed = cross(current[0].children[0], depth - 1);
                return local[0].children[0][0].text.as_str();
            }
        )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    {
        const auto prelude = std::string(R"(
            struct Node { text: String, other: String, children: Vector<[Node; 1]> }
            fn children(node: Node) -> [Node] => node.children[0];
            fn other_children(node: Node) -> [Node] => node.children[0];
        )");
        constexpr auto styles =
            std::array {"same helper", "other helper", "field access", "mixed access"};
        for (const auto depth : {1uz, 2uz, 4uz, 8uz}) {
            for (const auto style : {0uz, 1uz, 2uz, 3uz}) {
                auto steps = std::string();
                auto exact = std::string("root[0]");
                for (auto index = 0uz; index < depth; ++index) {
                    const auto helper = style == 1 ? "other_children" : "children";
                    steps += style == 2 || (style == 3 && index % 2 == 0)
                        ? "current = current[0].children[0];"
                        : std::format("current = {}(current[0]);", helper);
                    exact += ".children[0][0]";
                }
                for (const auto same_field : {false, true}) {
                    const auto source = prelude
                        + std::format(R"(
                        fn probe(&root: [Node; 1]) -> usize {{
                            let empty: [Node; 0] = [];
                            var current: [Node] = root;
                            {}
                            let view = current[0].text.as_str();
                            current = empty;
                            {}.{}.clear();
                            return view.byte_length();
                        }}
                    )",
                                      steps,
                                      exact,
                                      same_field ? "text" : "other");
                    TestRegistration(
                        std::format(
                            "Vector ownership: repeated finite helpers preserve exact backing and sibling separation / depth {} / {} / {}",
                            depth,
                            styles[style],
                            same_field ? "same field" : "sibling field"
                        )
                    ) = [source, same_field]() noexcept {
                        if (same_field) {
                            expect_diagnostic(
                                analyze_test_errors(source),
                                DiagnosticCode::AccessBorrowConflict
                            );
                        } else {
                            static_cast<void>(analyze_test_program(source));
                        }
                    };
                }
            }
        }
        "Vector ownership: repeated finite helpers preserve copies and independent siblings"_test =
            [source = prelude + R"(
            fn copied(&root: [Node; 1]) -> usize {
                var copy = root;
                let view = children(children(copy[0])[0])[0].text.as_str();
                root[0].children[0][0].children[0][0].text.clear();
                return view.byte_length();
            }
            fn independent(&root: [Node; 1]) -> usize {
                let view = children(children(root[0])[0])[0].text.as_str();
                root[0].children[1][0].text.clear();
                return view.byte_length();
            }
        )"]() noexcept {
                static_cast<void>(analyze_test_program(source));
            };
    }

    "Vector ownership: function effects retain caller readers and alias roles"_test =
        [] static noexcept {
            const auto prelude = std::string(R"(
            struct Node { text: String, other: String, children: Vector<Node> }
            struct Error {}
            fn change(&node: Node) { node.text.clear(); }
            fn maybe_change(&node: Node, fail: bool) throw Error {
                node.text.clear(); if fail { throw Error {}; }
            }
            fn borrow_change(a: Node, &b: Node) -> usize {
                let view = a.text.as_str(); b.text.clear(); return view.byte_length();
            }
            fn reset(&node: Node) { node.children.clear(); }
        )");
            for (const auto call :
                 {"change(&root.children[0]);",
                  "try { maybe_change(&root.children[0], fail)?; } catch { Error(error) => {}, }",
                  "reset(&root);"}) {
                expect_diagnostic(
                    analyze_test_errors(
                        prelude
                        + std::format(
                            R"(
                fn invalid(&root: Node, fail: bool) -> usize {{
                    let view = root.children[0].text.as_str(); {} return view.byte_length();
                }}
            )",
                            call
                        )
                    ),
                    DiagnosticCode::AccessBorrowConflict
                );
            }
            expect_diagnostic(
                analyze_test_errors(prelude + R"(
            fn unknown(&root: Node, index: usize) -> usize {
                return borrow_change(root.children[index], &root.children[0]);
            }
        )"),
                DiagnosticCode::AccessBorrowConflict
            );
            static_cast<void>(analyze_test_program(prelude + R"(
            fn separate(&root: Node) -> usize {
                return borrow_change(root.children[0], &root.children[1]);
            }
            fn release(&held: str, &text: String) {
                held = ""; text.clear();
            }
            fn released(&text: String) -> usize {
                var held = text.as_str(); release(&held, &text); return held.byte_length();
            }
        )"));
            expect_diagnostic(
                analyze_test_errors(R"(
            fn release(&held: str, &text: String) { held = ""; text.clear(); }
            fn still_borrowed(&text: String) -> usize {
                var held = text.as_str(); let independent = text.as_str();
                release(&held, &text); return independent.byte_length();
            }
        )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Vector ownership: acquisition protects guards without invalidating readers"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
            fn relay(predicate: fn() -> bool) -> bool => predicate();
            fn invalid(&selected: i32) {
                let callback = [&selected]() { return true; };
                match selected { _ if relay(callback) => {}, _ => {}, }
            }
        )"),
                DiagnosticCode::AccessOperationConflict
            );
            static_cast<void>(analyze_test_program(R"(
            fn noop(&text: String) {}
            fn accepted(&text: String) -> usize {
                let view = text.as_str(); noop(&text); return view.byte_length();
            }
            fn untouched(&values: Vector<i32>) {}
            fn iterate(&values: Vector<i32>) {
                for value in values { untouched(&values); }
            }
        )"));
        };

    "Vector ownership: divergence retains reached writes without a completion"_test =
        [] static noexcept {
            expect_diagnostic(
                analyze_test_errors(R"(
            fn spin(&text: String) { text.clear(); while true {} }
            fn caller(&text: String) { let view = text.as_str(); spin(&text); }
        )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Vector ownership: branching recursive components reuse formal interfaces"_test =
        [] static noexcept {
            for (const auto count : {1uz, 2uz, 4uz}) {
                auto source = std::string(
                    "struct Node { text: String, left: Vector<Node>, right: Vector<Node> }\n"
                );
                for (auto index = 0uz; index < count; ++index) {
                    source += std::format(
                        R"(
                    fn f{}(node: Node, depth: usize, choose: bool) -> str {{
                        if depth == 0 {{ return node.text.as_str(); }}
                        if choose {{ return f{}(node.left[0], depth - 1, choose); }}
                        return f{}(node.right[0], depth - 1, choose);
                    }}
                )",
                        index,
                        (index + 1uz) % count,
                        (index + 1uz) % count
                    );
                }
                const auto program = analyze_test_program(source);
                expect_bounded_ownership(program, count);
            }
        };

    "Vector ownership: possible arguments do not identify known siblings"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view target;
                std::string_view second;
                std::string_view field;
                bool caller_view;
                bool accepted;
            };
            constexpr auto cases = std::array {
                Case {
                    .name = "known siblings",
                    .target = "0",
                    .second = "1",
                    .field = "text",
                    .caller_view = false,
                    .accepted = true
                },
                Case {
                    .name = "unknown write can reach the reader",
                    .target = "index",
                    .second = "1",
                    .field = "text",
                    .caller_view = false,
                    .accepted = false
                },
                Case {
                    .name = "same child write conflicts",
                    .target = "1",
                    .second = "1",
                    .field = "text",
                    .caller_view = false,
                    .accepted = false
                },
                Case {
                    .name = "other field stays independent",
                    .target = "1",
                    .second = "1",
                    .field = "other",
                    .caller_view = false,
                    .accepted = true
                },
                Case {
                    .name = "caller reader survives a known sibling write",
                    .target = "0",
                    .second = "1",
                    .field = "text",
                    .caller_view = true,
                    .accepted = true
                },
                Case {
                    .name = "caller reader conflicts with an unknown write",
                    .target = "index",
                    .second = "1",
                    .field = "text",
                    .caller_view = true,
                    .accepted = false
                },
            };
            each(cases, &Case::name, [](const auto& item) static noexcept {
                const auto source = std::format(
                    R"(
                    struct Node {{ text: String, other: String, children: Vector<Node> }}
                    fn use(&first: Node, second: Node, ignored: Node) -> usize {{
                        let view = second.text.as_str();
                        first.{}.clear();
                        return view.byte_length();
                    }}
                    fn caller(&root: Node, index: usize) -> usize {{
                        {}
                        let result = use(&root.children[{}], root.children[{}], root.children[index]);
                        return result{};
                    }}
                )",
                    item.field,
                    item.caller_view ? "let view = root.children[1].text.as_str();" : "",
                    item.target,
                    item.second,
                    item.caller_view ? " + view.byte_length()" : ""
                );
                if (item.accepted) {
                    static_cast<void>(analyze_test_program(source));
                } else {
                    expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::AccessBorrowConflict
                    );
                }
            });
        };

    "Vector ownership: call projection retains ancestors through direct alias relays"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view target;
                bool release;
                bool accepted;
            };
            constexpr auto cases = std::array {
                Case {
                    .name = "unknown ancestor conflicts with a live Write holder",
                    .target = "index",
                    .release = false,
                    .accepted = false,
                },
                Case {
                    .name = "releasing the Write holder permits structural mutation",
                    .target = "index",
                    .release = true,
                    .accepted = true,
                },
                Case {
                    .name = "known sibling does not invalidate the holder",
                    .target = "1",
                    .release = false,
                    .accepted = true,
                },
            };
            each(cases, &Case::name, [](const auto& item) static noexcept {
                const auto source = std::format(
                    R"(
                    struct Node {{ text: String, left: Vector<Node>, right: Vector<Node> }}
                    fn clear(&node: Node, &held: str) {{
                        {}
                        node.right.clear();
                    }}
                    fn caller(&root: Node, index: usize) -> usize {{
                        var held = root.left[0].right[0].text.as_str();
                        clear(&root.left[{}], &held);
                        return held.byte_length();
                    }}
                )",
                    item.release ? "held = \"\";" : "",
                    item.target
                );
                if (item.accepted) {
                    static_cast<void>(analyze_test_program(source));
                } else {
                    expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::AccessBorrowConflict
                    );
                }
            });
        };

    "Vector ownership: recursive connectors preserve distinct terminal roles"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view second;
                std::string_view recursive_arguments;
                bool accepted;
            };
            constexpr auto cases = std::array {
                Case {
                    .name = "known siblings with an unused unknown ancestor",
                    .second = "1",
                    .recursive_arguments = "&first, &second",
                    .accepted = true,
                },
                Case {
                    .name = "swapped terminal roles retain sibling separation",
                    .second = "1",
                    .recursive_arguments = "&second, &first",
                    .accepted = true,
                },
                Case {
                    .name = "the same terminal remains a conflicting target",
                    .second = "0",
                    .recursive_arguments = "&first, &second",
                    .accepted = false,
                },
            };
            each(cases, &Case::name, [](const auto& item) static noexcept {
                const auto source = std::format(
                    R"(
                    struct Node {{ text: String, left: Vector<Node>, right: Vector<Node> }}
                    fn inspect(ignored: Node, &first: Node, &second: Node, depth: usize) -> usize {{
                        if depth > 0 {{
                            return inspect(ignored, {}, depth - 1);
                        }}
                        let view = second.text.as_str();
                        first.text.clear();
                        return view.byte_length();
                    }}
                    fn caller(&root: Node, index: usize, depth: usize) -> usize {{
                        return inspect(
                            root.left[index],
                            &root.left[0].right[0],
                            &root.left[{}].right[0],
                            depth
                        );
                    }}
                )",
                    item.recursive_arguments,
                    item.second
                );
                if (item.accepted) {
                    static_cast<void>(analyze_test_program(source));
                } else {
                    expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::AccessBorrowConflict
                    );
                }
            });
        };

    "Vector ownership: unused unknown selections preserve known return backing"_test =
        [] static noexcept {
            struct Case final {
                std::string_view name;
                std::string_view returned;
                std::string_view write;
                bool accepted;
            };
            constexpr auto cases = std::array {
                Case {
                    .name = "known sibling",
                    .returned = "0",
                    .write = "root.left[1].text.clear();",
                    .accepted = true,
                },
                Case {
                    .name = "other field",
                    .returned = "0",
                    .write = "root.left[0].other.clear();",
                    .accepted = true,
                },
                Case {
                    .name = "same field",
                    .returned = "0",
                    .write = "root.left[0].text.clear();",
                    .accepted = false,
                },
                Case {
                    .name = "unknown backing",
                    .returned = "index",
                    .write = "root.left[1].text.clear();",
                    .accepted = false,
                },
            };
            each(cases, &Case::name, [](const auto& item) static noexcept {
                const auto source = std::format(
                    R"(
                    struct Node {{ text: String, other: String, left: Vector<Node> }}
                    fn pick(node: Node, index: usize) -> str {{
                        node.left[index].text.as_str();
                        return node.left[{}].text.as_str();
                    }}
                    fn probe(&root: Node, index: usize) -> usize {{
                        let view = pick(root, index);
                        {}
                        return view.byte_length();
                    }}
                )",
                    item.returned,
                    item.write
                );
                if (item.accepted) {
                    static_cast<void>(analyze_test_program(source));
                } else {
                    expect_diagnostic(
                        analyze_test_errors(source),
                        DiagnosticCode::AccessBorrowConflict
                    );
                }
            });
        };

    "Vector ownership: recursive child and whole returns preserve borrowed regions"_test =
        [] static noexcept {
            const auto prelude = recursive_child_whole_borrows(false);
            static_cast<void>(analyze_test_program(prelude + R"(
                fn borrow(root: Node, depth: usize, choose: bool) -> str {
                    return f0(root.left[0], root, depth, choose);
                }
                fn siblings(&root: Node, depth: usize, choose: bool) -> usize {
                    let view = borrow(root, depth, choose);
                    root.left[0].other.clear();
                    root.left[1].text.clear();
                    root.right[0].text.clear();
                    return view.byte_length();
                }
            )"));
            expect_diagnostic(
                analyze_test_errors(prelude + R"(
                fn invalid(&root: Node, depth: usize, choose: bool) -> usize {
                    let view = f0(root.left[0], root, depth, choose);
                    root.left[0].text.clear();
                    return view.byte_length();
                }
            )"),
                DiagnosticCode::AccessBorrowConflict
            );
        };

    "Vector ownership: recursive child and whole failures preserve borrowed regions"_test =
        [] static noexcept {
            const auto prelude = recursive_child_whole_borrows(true);
            constexpr auto caller = [](std::string_view success,
                                       std::string_view failure) static noexcept {
                return std::format(
                    R"(
                    fn probe(&root: Node, depth: usize, choose: bool, fail: bool) -> usize {{
                        try {{
                            let view = f0(root.left[0], root, depth, choose, fail)?;
                            {}
                            return view.byte_length();
                        }} catch {{
                            ViewError(error) => {{
                                {}
                                return error.text.byte_length();
                            }},
                        }}
                    }}
                )",
                    success,
                    failure
                );
            };
            constexpr auto siblings = std::string_view(
                "root.left[0].other.clear(); root.left[1].text.clear(); root.right[0].text.clear();"
            );
            static_cast<void>(analyze_test_program(prelude + caller(siblings, siblings)));
            expect_diagnostic(
                analyze_test_errors(prelude + caller("root.left[0].text.clear();", "")),
                DiagnosticCode::AccessBorrowConflict
            )
                .note("completion = normal");
            expect_diagnostic(
                analyze_test_errors(prelude + caller("", "root.left[0].text.clear();")),
                DiagnosticCode::AccessBorrowConflict
            )
                .note("completion = failure");
        };

    "Vector ownership: recursive ancestor and child arguments have bounded analysis"_test =
        [] static noexcept {
            for (const auto count : {1uz, 2uz, 4uz}) {
                auto source = std::string(
                    "struct Node { value: i32, left: Vector<Node>, right: Vector<Node> }\n"
                );
                for (auto index = 0uz; index < count; ++index) {
                    source += std::format(
                        R"(
                    fn increment{}(&node: Node, &whole: Node, choose: bool, depth: usize) -> void {{
                        node.value += 1;
                        whole.value += 1;
                        if depth == 0 {{ return; }}
                        if choose {{ increment{}(&node.left[0], &whole, choose, depth - 1); }}
                        else {{ increment{}(&node.right[0], &whole, choose, depth - 1); }}
                    }}
                )",
                        index,
                        (index + 1uz) % count,
                        (index + 1uz) % count
                    );
                }
                source += R"(
                fn caller(&first: Node, &second: Node, choose: bool, depth: usize) {
                    increment0(&first, &first, choose, depth);
                    increment0(&first, &second, choose, depth);
                }
            )";
                const auto program = analyze_test_program(source);
                expect_bounded_ownership(program, count);
            }
        };

    "Vector ownership: role permutations retain return and failure precision"_test =
        [] static noexcept {
            const auto source = std::string(R"(
                struct Node { text: String, other: String, left: Vector<Node>, right: Vector<Node> }
                struct ViewError { text: str }
                fn rotate(node: Node, whole: Node, depth: usize, choose: bool, fail: bool)
                    -> str throw ViewError {
                    if depth == 0 {
                        if fail { throw ViewError { text: node.text.as_str() }; }
                        return node.text.as_str();
                    }
                    if choose { return rotate(whole, node.left[0], depth - 1, choose, fail)?; }
                    return rotate(whole, node.right[0], depth - 1, choose, fail)?;
                }
            )");
            constexpr auto caller = [](std::string_view success,
                                       std::string_view failure) static noexcept {
                return std::format(
                    R"(
                    fn probe(&root: Node, depth: usize, choose: bool, fail: bool) -> usize {{
                        try {{
                            let view = rotate(root.left[0], root, depth, choose, fail)?;
                            {}
                            return view.byte_length();
                        }} catch {{
                            ViewError(error) => {{ {} return error.text.byte_length(); }},
                        }}
                    }}
                )",
                    success,
                    failure
                );
            };
            constexpr auto independent = std::string_view(
                "root.left[0].other.clear(); root.left[1].text.clear(); root.right[1].text.clear();"
            );
            static_cast<void>(analyze_test_program(source + caller(independent, independent)));
            expect_diagnostic(
                analyze_test_errors(source + caller("root.right[0].text.clear();", "")),
                DiagnosticCode::AccessBorrowConflict
            )
                .note("completion = normal");
            expect_diagnostic(
                analyze_test_errors(source + caller("", "root.left[0].text.clear();")),
                DiagnosticCode::AccessBorrowConflict
            )
                .note("completion = failure");
        };

    "Vector literals: repeated Take cannot consume an owner twice"_test = [] static noexcept {
        expect_diagnostic(
            analyze_test_errors(R"(
                fn invalid() {
                    let text = String::from_str("owned");
                    let values: Vector<String> = [&&text; 2];
                }
            )"),
            DiagnosticCode::AccessUnavailable
        );
    };

    "Vector views: loans protect owners and cannot escape local backing"_test = [] static noexcept {
        constexpr auto cases = std::array {
            R"(fn bad() { var v: Vector<i32> = [1, 2]; let view: [i32] = v; v.reserve(2); })",
            R"(fn bad() { var v: Vector<i32> = [1, 2]; v.append(v); })",
            R"(fn bad() { var v: Vector<i32> = [1, 2]; let view = v.slice(0, 1); v[1] = 3; })",
            R"(fn bad() { let v: Vector<i32> = [1, 2]; let view: [i32] = v; let moved = &&v; })",
            R"(fn bad() -> [i32] { let v: Vector<i32> = [1, 2]; return v; })",
            R"(fn bad() { let view: [i32] = Vector<i32>::from_slice([1, 2]); })",
        };
        each(cases, std::identity {}, [](const auto& source) static noexcept {
            expect_diagnostic(analyze_test_errors(source), DiagnosticCode::AccessBorrowConflict);
        });
    };

    "Vector execution: owning storage and generic elements execute and retain their type"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                const fn make<T>(&&value: T) -> Vector<T> {
                    var values = Vector<T> {};
                    values.push(&&value);
                    return &&values;
                }
                const fn count() -> usize {
                    var values = make(&&7);
                    values.push(9);
                    values.remove(0);
                    return values.length();
                }
                const answer = count();
                const empty = Vector<i32> {}.is_empty();
            )"));
            static_cast<void>(analyze_test_program(R"(
                enum Node { Leaf(i32), Branch(Vector<Node>) }
                const fn leaf_count(node: Node) -> i32 {
                    match node {
                        .Leaf(_) => return 1,
                        .Branch(children) => {
                            var count = 0;
                            for child in children { count += leaf_count(child); }
                            return count;
                        },
                    }

                }
                const fn document() -> i32 {
                    var branches = Vector<Node> {};
                    for _ in 0..4 {
                        var leaves = Vector<Node> {};
                        for number in 0..32 { leaves.push(Node::Leaf(number)); }
                        branches.push(Node::Branch(&&leaves));
                    }
                    return leaf_count(Node::Branch(&&branches));
                }
                const count = document();
            )"));
            static_cast<void>(analyze_test_program(R"(
                enum Choice { Small(i32), Large([i32; 4096]) }
                const fn count() -> usize {
                    var values = Vector<Choice> {};
                    for value in 0..64 { values.push(Choice::Small(value)); }
                    return values.length();
                }
                const result = count();
            )"));
            static_cast<void>(analyze_test_program(R"(
                enum Choice { Small(i32), Large([i32; 65536]) }
                struct Holder { choices: Vector<Choice> }
                const fn read(value: Choice) -> i32 {
                    return match value { .Small(number) => number, .Large(_) => 0 };
                }
                const fn copy_small() -> i32 {
                    let small = Choice::Small(7);
                    let copied = small;
                    var owner = Holder {};
                    owner.choices.push(copied);
                    let snapshot = owner;
                    owner.choices.clear();
                    return read(snapshot.choices[0]);
                }
                const result = copy_small();
            )"));
            static_cast<void>(analyze_test_program("const values = Vector<i32> {};"));
            expect_diagnostic(
                analyze_test_errors(R"(
                const fn invalid() -> usize {
                    var values = Vector<i32> {};
                    values.remove(0);
                    return values.length();
                }
                const result = invalid();
            )"),
                DiagnosticCode::ConstIndexBounds
            );
        };

    "Vector execution: bounded dynamic variants complete and released storage permits reuse"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
                enum Tree { Leaf, Branch(Vector<Tree>) }
                const {
                    var leaves = Vector<Tree> {};
                    for _ in 0..3 { leaves.push(Tree::Leaf); }
                    assert(leaves.length() == 3);
                }
                const {
                    var rows = Vector<[i32; 2]> {};
                    for _ in 0..2 { rows.push([0; 2]); }
                    var groups = Vector<Vector<[i32; 2]>> {};
                    groups.push(rows);
                    let removed = groups.remove(0);
                    assert(removed.length() == 2);
                    groups.push(rows);
                    groups.clear();
                    for _ in 0..3 { groups.push(rows); }
                    groups[0] = [];
                    groups.push(rows);
                    assert(groups.length() == 4 && groups[0].is_empty());
                }
                const {
                    var rows = Vector<[i32; 2]> {};
                    for _ in 0..2 { rows.push([0; 2]); }
                    var (first, second) = (rows, rows);
                    first.remove(0);
                    let moved = &&first;
                    second.append(rows);
                    second.append(rows);
                    assert(moved.length() == 1 && second.length() == 6);
                }
            )"));
        };

    {
        const auto prelude = std::string(R"(
                struct Node { children: Vector<Node> }
                const fn build(depth: i32) -> Node {
                    var node = Node {};
                    for _ in 0..depth {
                        var children = Vector<Node> {};
                        children.push(&&node);
                        node = Node { children: &&children };
                    }
                    return &&node;
                }
            )");
        "Vector execution: dynamic storage bounds accept finite Take construction"_test =
            [source = prelude + R"(
                const count = build(8).children.length();
            )"]() noexcept {
                static_cast<void>(analyze_test_program(source));
            };
        struct Input final {
            std::string_view name;
            std::string_view source;
        };
        constexpr auto cases = std::array {
            Input {
                "deep Take construction",
                R"(const count = build(40).children.length();)",
            },
            Input {
                "element replacement",
                R"(const fn grow() -> usize {
                    var root = Node {};
                    root.children.push(Node {});
                    root.children[0] = build(31);
                    return root.children.length();
                }
                const count = grow();)",
            },
            Input {
                "enum payload growth",
                R"(enum Tree { Leaf, Branch(Vector<[i32; 1024]>) }
                const fn grow() -> usize {
                    var tree = Tree::Branch([]);
                    match tree {
                        .Branch(&rows) => {
                            for _ in 0..64 { rows.push([0; 1024]); }
                            return rows.length();
                        },
                        .Leaf => return 0,
                    }
                }
                const count = grow();)",
            },
        };
        for (const auto input : cases) {
            TestRegistration(
                std::format(
                    "Vector execution: dynamic storage bounds cover Take construction and nested Write / {}",
                    input.name
                )
            ) = [source = prelude + std::string(input.source)]() noexcept {
                expect_diagnostic(analyze_test_errors(source), DiagnosticCode::ConstLimit);
            };
        }
    }
});

} // namespace
