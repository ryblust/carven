module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.default_initialization;

import :semantic.semir.structured;
import :semantic.semir.traversal;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE("Defaults: semantic size is independent of array extent") {
    const auto program = analyze_test_program(R"(
        struct Small { values: [i32; 1] }
        struct Large { values: [i32; 100000000] }
        fn small() -> Small { return Small {}; }
        fn large() -> Large { return Large {}; }
    )");
    auto sizes = std::vector<std::size_t>();
    for (const auto [id, body] : program.bodies().entries()) {
        static_cast<void>(id);
        auto count = 0uz;
        visit_semantic_nodes(body.region(), [&](const SemanticExpression&) noexcept { ++count; });
        sizes.push_back(count);
    }
    REQUIRE(sizes.size() == 2uz);
    CHECK(sizes[0] == sizes[1]);
}
