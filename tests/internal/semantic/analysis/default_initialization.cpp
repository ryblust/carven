module carven:test.internal.semantic.analysis.default_initialization;

import :diagnostics.code;
import :semantic.semir.structured;
import :semantic.semir.traversal;
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
            values: Sequence<T>,
            empty: [T; 0],
            pointer: ptr<T>,
            view: [T],
        }
        fn storage<T>() -> Storage<T> => Storage<T> {};
        enum FixedElement { Item(i32) }
        struct Holder<T> { values: Sequence<T>, fixed: Sequence<FixedElement> }
        const fn make<T>() -> Holder<T> => Holder<T> {};
        const fixed_count = make<i32>().fixed.len();
        const count = make<NoDefault>().values.len();
        fn empty<T>() -> [T; 0] { return {}; }
    )"));
            const auto cases = std::array {
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

    "Semantic defaults: semantic size is independent of array extent"_test = [] static noexcept {
        const auto program = analyze_test_program(R"(
        struct Small { values: [i32; 1] }
        struct Large { values: [i32; 100000000] }
        fn small() -> Small { return {}; }
        fn large() -> Large { return {}; }
    )");
        auto sizes = std::vector<std::size_t>();
        for (const auto [id, body] : program.bodies().entries()) {
            static_cast<void>(id);
            auto count = 0uz;
            visit_semantic_nodes(body.region(), [&](const SemanticExpression&) noexcept {
                ++count;
            });
            sizes.push_back(count);
        }
        if (!expect(sizes.size() == 2uz)) {
            return;
        }
        expect(sizes[0] == sizes[1]);
    };
});

} // namespace
