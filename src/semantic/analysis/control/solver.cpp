module carven:semantic.analysis.control.solver.impl;

import :semantic.analysis.control.internal;
import :semantic.hir.type;
import :support.graph;
import std;

namespace {

auto union_failures(std::span<const HIRTypeID> left, std::span<const HIRTypeID> right) noexcept
    -> std::vector<HIRTypeID> {
    auto result = std::vector<HIRTypeID>();
    result.reserve(left.size() + right.size());
    auto left_index = 0uz;
    auto right_index = 0uz;
    while (left_index < left.size() && right_index < right.size()) {
        const auto left_id = left[left_index].index();
        const auto right_id = right[right_index].index();
        if (left_id < right_id) {
            result.push_back(left[left_index++]);
        } else if (right_id < left_id) {
            result.push_back(right[right_index++]);
        } else {
            result.push_back(left[left_index++]);
            ++right_index;
        }
    }
    result.insert(result.end(), left.begin() + static_cast<std::ptrdiff_t>(left_index), left.end());
    result.insert(
        result.end(),
        right.begin() + static_cast<std::ptrdiff_t>(right_index),
        right.end()
    );
    return result;
}

} // namespace

auto solve_failure_contracts(SemanticConstruction& builder) noexcept
    -> std::vector<std::vector<HIRTypeID>> {
    auto failure_sets = std::vector<std::vector<HIRTypeID>>(builder.callables().size());
    for (auto index = 0uz; index < builder.callables().size(); ++index) {
        failure_sets[index] = builder.failure_set(builder.callables()[index].failure_set).members;
    }

    auto dependencies = std::vector<std::vector<std::uint32_t>>(builder.callables().size());
    for (auto index = 0uz; index < builder.callables().size(); ++index) {
        const auto callable = CallableID::from_index(static_cast<std::uint32_t>(index));
        if (builder.callable(callable).failure_contract == HIRFailureContractKind::Inferred) {
            static_cast<void>(
                evaluate_callable_control(builder, failure_sets, callable, &dependencies)
            );
        }
    }

    auto reverse_callers = std::vector<std::vector<std::uint32_t>>(dependencies.size());
    for (auto caller = 0uz; caller < dependencies.size(); ++caller) {
        auto& callees = dependencies[caller];
        std::ranges::sort(callees);
        callees.erase(std::ranges::unique(callees).begin(), callees.end());
        for (const auto callee : callees) {
            reverse_callers[callee].push_back(static_cast<std::uint32_t>(caller));
        }
    }
    for (auto& callers : reverse_callers) {
        std::ranges::sort(callers);
        callers.erase(std::ranges::unique(callers).begin(), callers.end());
    }

    const auto components = strongly_connected_components(std::move(dependencies));
    auto queued = std::vector<std::uint8_t>(builder.callables().size(), 0);
    for (auto component_index = 0uz; component_index < components.dependency_first.size();
         ++component_index) {
        const auto& component = components.dependency_first[component_index];
        auto worklist = std::deque<std::uint32_t>();
        const auto enqueue = [&](std::uint32_t member) noexcept {
            const auto callable = CallableID::from_index(member);
            if (builder.callable(callable).failure_contract == HIRFailureContractKind::Inferred
                && queued[member] == 0) {
                queued[member] = 1;
                worklist.push_back(member);
            }
        };
        for (const auto member : component) {
            enqueue(member);
        }
        while (!worklist.empty()) {
            const auto member = worklist.front();
            worklist.pop_front();
            queued[member] = 0;
            const auto summary = evaluate_callable_control(
                builder,
                failure_sets,
                CallableID::from_index(member),
                nullptr
            );
            auto inferred = union_failures(failure_sets[member], summary.outward_failures);
            if (inferred == failure_sets[member]) {
                continue;
            }
            failure_sets[member] = std::move(inferred);
            for (const auto caller : reverse_callers[member]) {
                if (components.component_of[caller] == component_index) {
                    enqueue(caller);
                }
            }
        }
    }
    return failure_sets;
}
