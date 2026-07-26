module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.support.graph;

import :support.graph;
import std;

TEST_CASE("Support graph: strong components are dependency-first and deterministic") {
    auto adjacency = std::vector<std::vector<std::uint32_t>> {
        {1, 1},
        {0},
        {0},
        {2},
        {},
    };
    const auto result = strongly_connected_components(std::move(adjacency));
    CHECK_EQ(
        result.dependency_first,
        std::vector<std::vector<std::uint32_t>> {{0, 1}, {2}, {3}, {4}}
    );
    CHECK_EQ(result.component_of, std::vector<std::uint32_t> {0, 0, 1, 2, 3});
}

TEST_CASE("Support graph: long dependency chains use the explicit DFS stack") {
    constexpr auto node_count = 65'536u;
    auto adjacency = std::vector<std::vector<std::uint32_t>>(node_count);
    for (auto node = 0u; node + 1 < node_count; ++node) {
        adjacency[node].push_back(node + 1);
    }

    const auto result = strongly_connected_components(std::move(adjacency));
    REQUIRE_EQ(result.dependency_first.size(), node_count);
    CHECK_EQ(result.dependency_first.front(), std::vector<std::uint32_t> {node_count - 1});
    CHECK_EQ(result.dependency_first.back(), std::vector<std::uint32_t> {0});
    CHECK_EQ(result.component_of.front(), node_count - 1);
    CHECK_EQ(result.component_of.back(), 0u);
}

TEST_CASE("Support graph: chains, isolated nodes, and self edges retain canonical order") {
    SUBCASE("dependency chain") {
        const auto result =
            strongly_connected_components(std::vector<std::vector<std::uint32_t>> {{1}, {2}, {}});
        CHECK_EQ(result.dependency_first, std::vector<std::vector<std::uint32_t>> {{2}, {1}, {0}});
        CHECK_EQ(result.component_of, std::vector<std::uint32_t> {2, 1, 0});
    }
    SUBCASE("isolated nodes") {
        const auto result =
            strongly_connected_components(std::vector<std::vector<std::uint32_t>> {{}, {}, {}});
        CHECK_EQ(result.dependency_first, std::vector<std::vector<std::uint32_t>> {{0}, {1}, {2}});
        CHECK_EQ(result.component_of, std::vector<std::uint32_t> {0, 1, 2});
    }
    SUBCASE("self edge") {
        const auto result =
            strongly_connected_components(std::vector<std::vector<std::uint32_t>> {{0}});
        CHECK_EQ(result.dependency_first, std::vector<std::vector<std::uint32_t>> {{0}});
        CHECK_EQ(result.component_of, std::vector<std::uint32_t> {0});
    }
}
