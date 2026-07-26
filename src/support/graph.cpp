module carven:support.graph.impl;

import :support.graph;
import :support.invariant;
import std;

auto strongly_connected_components(std::vector<std::vector<std::uint32_t>> adjacency) noexcept
    -> StrongComponents {
    if (adjacency.size() > std::numeric_limits<std::uint32_t>::max()) {
        resource_limit_exceeded("graph exhausted its 32-bit identity space");
    }
    for (auto& targets : adjacency) {
        std::ranges::sort(targets);
        const auto unique = std::ranges::unique(targets);
        targets.erase(unique.begin(), unique.end());
        if (std::ranges::any_of(targets, [&](std::uint32_t target) noexcept {
                return target >= adjacency.size();
            })) {
            invariant_violation("strong-component graph contains an invalid edge");
        }
    }

    struct DepthFrame final {
        std::uint32_t node;
        std::size_t next_target;
    };

    const auto count = adjacency.size();
    constexpr auto unvisited = std::numeric_limits<std::uint32_t>::max();
    auto next_index = 0u;
    auto indices = std::vector<std::uint32_t>(count, unvisited);
    auto low_links = std::vector<std::uint32_t>(count);
    auto active = std::vector<std::uint8_t>(count);
    auto stack = std::vector<std::uint32_t>();
    auto components = std::vector<std::vector<std::uint32_t>>();

    const auto enter = [&](std::uint32_t node) noexcept {
        indices[node] = next_index;
        low_links[node] = next_index;
        ++next_index;
        stack.push_back(node);
        active[node] = 1;
    };

    auto depth = std::vector<DepthFrame>();
    for (auto index = 0uz; index < count; ++index) {
        if (indices[index] != unvisited) {
            continue;
        }
        const auto root = static_cast<std::uint32_t>(index);
        enter(root);
        depth.push_back({.node = root, .next_target = 0});
        while (!depth.empty()) {
            auto& frame = depth.back();
            const auto node = frame.node;
            if (frame.next_target < adjacency[node].size()) {
                const auto target = adjacency[node][frame.next_target];
                ++frame.next_target;
                if (indices[target] == unvisited) {
                    enter(target);
                    depth.push_back({.node = target, .next_target = 0});
                } else if (active[target] != 0) {
                    low_links[node] = std::min(low_links[node], indices[target]);
                }
                continue;
            }

            depth.pop_back();
            if (low_links[node] == indices[node]) {
                auto component = std::vector<std::uint32_t>();
                while (true) {
                    const auto member = stack.back();
                    stack.pop_back();
                    active[member] = 0;
                    component.push_back(member);
                    if (member == node) {
                        break;
                    }
                }
                std::ranges::sort(component);
                components.push_back(std::move(component));
            }
            if (!depth.empty()) {
                const auto parent = depth.back().node;
                low_links[parent] = std::min(low_links[parent], low_links[node]);
            }
        }
    }

    auto original_component_of = std::vector<std::uint32_t>(count);
    for (auto component = 0uz; component < components.size(); ++component) {
        for (const auto member : components[component]) {
            original_component_of[member] = static_cast<std::uint32_t>(component);
        }
    }
    auto dependents = std::vector<std::vector<std::uint32_t>>(components.size());
    auto dependency_count = std::vector<std::uint32_t>(components.size());
    for (auto source = 0uz; source < count; ++source) {
        const auto source_component = original_component_of[source];
        for (const auto target : adjacency[source]) {
            const auto target_component = original_component_of[target];
            if (source_component == target_component) {
                continue;
            }
            dependents[target_component].push_back(source_component);
        }
    }
    for (auto& values : dependents) {
        std::ranges::sort(values);
        values.erase(std::ranges::unique(values).begin(), values.end());
        for (const auto dependent : values) {
            ++dependency_count[dependent];
        }
        std::ranges::sort(values, {}, [&](std::uint32_t component) noexcept {
            return components[component].front();
        });
    }

    auto ready = std::set<std::pair<std::uint32_t, std::uint32_t>>();
    for (auto component = 0uz; component < components.size(); ++component) {
        if (dependency_count[component] == 0) {
            ready.emplace(components[component].front(), static_cast<std::uint32_t>(component));
        }
    }
    auto ordered = std::vector<std::vector<std::uint32_t>>();
    ordered.reserve(components.size());
    auto final_index = std::vector<std::uint32_t>(components.size());
    while (!ready.empty()) {
        const auto component = ready.begin()->second;
        ready.erase(ready.begin());
        final_index[component] = static_cast<std::uint32_t>(ordered.size());
        ordered.push_back(std::move(components[component]));
        for (const auto dependent : dependents[component]) {
            --dependency_count[dependent];
            if (dependency_count[dependent] == 0) {
                ready.emplace(components[dependent].front(), dependent);
            }
        }
    }
    if (ordered.size() != components.size()) {
        invariant_violation("strong-component condensation graph is cyclic");
    }

    auto component_of = std::vector<std::uint32_t>(count);
    for (auto node = 0uz; node < count; ++node) {
        component_of[node] = final_index[original_component_of[node]];
    }
    return {
        .dependency_first = std::move(ordered),
        .component_of = std::move(component_of),
    };
}
