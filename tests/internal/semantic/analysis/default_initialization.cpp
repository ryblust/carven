module carven:test.internal.semantic.analysis.default_initialization;

import :semantic.semir.structured;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Semantic defaults: semantic size is independent of array extent", [] static noexcept {
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
        if (!ct::expect(sizes.size() == 2uz)) {
            return;
        }
        ct::expect(sizes[0] == sizes[1]);
    });
});

} // namespace
