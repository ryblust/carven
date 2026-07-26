module carven:support.graph;

import std;

struct StrongComponents final {
    std::vector<std::vector<std::uint32_t>> dependency_first;
    std::vector<std::uint32_t> component_of;
};

auto strongly_connected_components(std::vector<std::vector<std::uint32_t>> adjacency) noexcept
    -> StrongComponents;
