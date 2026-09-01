module carven:semantic.analysis.availability.graph.impl;

import :semantic.analysis.availability.graph;
import std;

auto MutableAvailabilityGraph::append(
    std::vector<AvailabilityOperation> operations,
    std::vector<AvailabilityBlockID> successors
) noexcept -> AvailabilityBlockID {
    normalize_successors(successors);
    const auto id = AvailabilityBlockID {
        .value = static_cast<std::uint32_t>(blocks.size()),
    };
    blocks.push_back({
        .operations = std::move(operations),
        .successors = std::move(successors),
    });
    return id;
}

auto MutableAvailabilityGraph::set_successors(
    AvailabilityBlockID id,
    std::vector<AvailabilityBlockID> successors
) noexcept -> void {
    normalize_successors(successors);
    blocks[id.value].successors = std::move(successors);
}

auto MutableAvailabilityGraph::freeze(AvailabilityBlockID entry) && noexcept
    -> BodyAvailabilityGraph {
    auto reachable = std::vector<std::uint8_t>(blocks.size(), 0);
    auto pending = std::vector<AvailabilityBlockID> {entry};
    while (!pending.empty()) {
        const auto block = pending.back();
        pending.pop_back();
        if (reachable[block.value] != 0) {
            continue;
        }
        reachable[block.value] = 1;
        for (const auto successor : blocks[block.value].successors) {
            pending.push_back(successor);
        }
    }

    auto indegree = std::vector<std::uint32_t>(blocks.size(), 0);
    for (auto index = 0uz; index < blocks.size(); ++index) {
        if (reachable[index] == 0) {
            continue;
        }
        for (const auto successor : blocks[index].successors) {
            if (reachable[successor.value] != 0) {
                ++indegree[successor.value];
            }
        }
    }

    constexpr auto unassigned = std::numeric_limits<std::uint32_t>::max();
    auto frozen_of = std::vector<std::uint32_t>(blocks.size(), unassigned);
    auto chains = std::vector<std::vector<AvailabilityBlockID>>();
    const auto append_chain = [&](AvailabilityBlockID start) noexcept {
        const auto frozen = static_cast<std::uint32_t>(chains.size());
        auto chain = std::vector<AvailabilityBlockID>();
        auto current = start;
        while (true) {
            frozen_of[current.value] = frozen;
            chain.push_back(current);
            const auto& source = blocks[current.value];
            if (source.successors.size() != 1) {
                break;
            }
            const auto next = source.successors.front();
            if (reachable[next.value] == 0
                || indegree[next.value] != 1
                || frozen_of[next.value] != unassigned) {
                break;
            }
            current = next;
        }
        chains.push_back(std::move(chain));
    };

    append_chain(entry);
    for (auto index = 0uz; index < blocks.size(); ++index) {
        if (reachable[index] != 0 && frozen_of[index] == unassigned) {
            append_chain(
                AvailabilityBlockID {
                    .value = static_cast<std::uint32_t>(index),
                }
            );
        }
    }

    auto result = BodyAvailabilityGraph {
        .entry = AvailabilityBlockID {.value = frozen_of[entry.value]},
        .blocks = {},
        .operations = {},
        .successors = {},
    };
    result.blocks.reserve(chains.size());
    for (const auto& chain : chains) {
        const auto operation_begin = static_cast<std::uint32_t>(result.operations.size());
        for (const auto source : chain) {
            auto& operations = blocks[source.value].operations;
            result.operations.insert(
                result.operations.end(),
                std::make_move_iterator(operations.begin()),
                std::make_move_iterator(operations.end())
            );
        }
        const auto successor_begin = static_cast<std::uint32_t>(result.successors.size());
        auto successors = std::vector<AvailabilityBlockID>();
        for (const auto successor : blocks[chain.back().value].successors) {
            successors.push_back(
                AvailabilityBlockID {
                    .value = frozen_of[successor.value],
                }
            );
        }
        normalize_successors(successors);
        result.successors.insert(result.successors.end(), successors.begin(), successors.end());
        result.blocks.push_back({
            .operation_begin = operation_begin,
            .operation_count =
                static_cast<std::uint32_t>(result.operations.size()) - operation_begin,
            .successor_begin = successor_begin,
            .successor_count =
                static_cast<std::uint32_t>(result.successors.size()) - successor_begin,
        });
    }
    return result;
}

auto MutableAvailabilityGraph::normalize_successors(
    std::vector<AvailabilityBlockID>& successors
) noexcept -> void {
    std::ranges::sort(successors, {}, &AvailabilityBlockID::value);
    successors.erase(std::ranges::unique(successors).begin(), successors.end());
}
