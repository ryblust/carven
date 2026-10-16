module carven:test.internal.semantic.analysis.default_initialization;

import :diagnostics.code;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Semantic defaults: generic shapes require only their initialized components"_test =
        [] static noexcept {
            static_cast<void>(analyze_test_program(R"(
        enum NoDefault { Item(i32) }
        struct Storage<T> {
            values: Vector<T>,
            empty: [T; 0],
            pointer: ptr<T>,
            view: [T],
        }
        fn storage<T>() -> Storage<T> => Storage<T> {};
        struct Link<T> { back: ptr<Node> }
        const absent: ptr<Node> = nullptr;
        struct Node {
            link: Link<i32>,
            values: [i32; if absent == nullptr { 1usize } else { 0usize }],
        }
        const node = Node {};
        const { assert(node.link.back == nullptr && node.values.length() == 1usize); }
        enum FixedElement { Item(i32) }
        struct Holder<T> { values: Vector<T>, fixed: Vector<FixedElement> }
        const fn make<T>() -> Holder<T> => Holder<T> {};
        const fn count_fixed<T>() -> usize => make<T>().fixed.length();
        const nested_count = count_fixed<i32>();
        const fixed_count = make<i32>().fixed.length();
        const count = make<NoDefault>().values.length();
        const { assert(nested_count == 0usize && fixed_count == 0usize && count == 0usize); }
        const fn empty<T>() -> [T; 0] { return {}; }
        const no_elements = empty<NoDefault>();
        const { assert(no_elements.length() == 0usize); }
    )"));
            constexpr auto cases = std::array {
                "fn invalid<T>() -> T { return {}; }",
                "fn invalid<T>() -> [T; 1] { return {}; }",
                "struct Holder<T> { value: T } fn invalid<T>() -> Holder<T> { return {}; }",
                "enum Choice<T> { Item(T), Empty } fn invalid<T>() -> Choice<T> { return {}; }",
                "class C { value: i32 } struct Holder<T> { value: C } fn invalid<T>() -> Holder<T> { return {}; }",
            };
            each(cases, std::identity {}, [](const auto& source) noexcept {
                expect_diagnostic(
                    analyze_test_errors(source),
                    DiagnosticCode::TypeDefaultInitialization
                );
            });
        };
});

} // namespace
