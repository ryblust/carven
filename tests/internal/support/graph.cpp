module carven:test.internal.support.graph;

import :support.graph;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Support graph: strong components are dependency-first and deterministic"_test =
        [] static noexcept {
            auto adjacency = std::vector<std::vector<std::uint32_t>> {
                {1, 1},
                {0},
                {0},
                {2},
                {},
            };
            const auto result = strongly_connected_components(std::move(adjacency));
            expect(
                (result.dependency_first
                 == std::vector<std::vector<std::uint32_t>> {{0, 1}, {2}, {3}, {4}})
            );
            expect((result.component_of == std::vector<std::uint32_t> {0, 0, 1, 2, 3}));
        };

    "Support graph: long dependency chains use the explicit DFS stack"_test = [] static noexcept {
        constexpr auto node_count = 65'536u;
        auto adjacency = std::vector<std::vector<std::uint32_t>>(node_count);
        for (auto node = 0u; node + 1 < node_count; ++node) {
            adjacency[node].push_back(node + 1);
        }

        const auto result = strongly_connected_components(std::move(adjacency));
        if (!expect_equal(result.dependency_first.size(), node_count)) {
            return;
        }
        expect((result.dependency_first.front() == std::vector<std::uint32_t> {node_count - 1}));
        expect((result.dependency_first.back() == std::vector<std::uint32_t> {0}));
        expect_equal(result.component_of.front(), node_count - 1);
        expect_equal(result.component_of.back(), 0u);
    };

    "Support graph: dependency chains place dependencies first"_test = [] static noexcept {
        const auto result =
            strongly_connected_components(std::vector<std::vector<std::uint32_t>> {{1}, {2}, {}});
        expect(
            (result.dependency_first == std::vector<std::vector<std::uint32_t>> {{2}, {1}, {0}})
        );
        expect((result.component_of == std::vector<std::uint32_t> {2, 1, 0}));
    };

    "Support graph: isolated nodes preserve source order"_test = [] static noexcept {
        const auto result =
            strongly_connected_components(std::vector<std::vector<std::uint32_t>> {{}, {}, {}});
        expect(
            (result.dependency_first == std::vector<std::vector<std::uint32_t>> {{0}, {1}, {2}})
        );
        expect((result.component_of == std::vector<std::uint32_t> {0, 1, 2}));
    };

    "Support graph: self edges form one component"_test = [] static noexcept {
        const auto result =
            strongly_connected_components(std::vector<std::vector<std::uint32_t>> {{0}});
        expect((result.dependency_first == std::vector<std::vector<std::uint32_t>> {{0}}));
        expect((result.component_of == std::vector<std::uint32_t> {0}));
    };
});

} // namespace
