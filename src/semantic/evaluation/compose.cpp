module carven:semantic.evaluation.compose.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.value;
import :semantic.semir.type;
import :support.invariant;
import std;

auto SemanticExecutor::compose(
    ExecutionCompositionAction action,
    std::vector<ExecutionOperand> arguments,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto cancelled = [&]() noexcept -> ExecutionFailure {
        return ExecutionCancelled {
            .origin = origin,
            .calls = current->calls,
            .blocks = current->blocks,
        };
    };
    if (cancel_requested()) {
        co_return std::unexpected(cancelled());
    }
    auto success = type(action.success, origin);
    if (!success) {
        co_return std::unexpected(std::move(success.error()));
    }

    auto children = std::vector<std::list<ExecutionLogicalTask>::iterator>();
    children.reserve(arguments.size());
    auto* parent = current;
    const auto start = [&](ExecutionTask<ExecutionValue> operation) noexcept {
        tasks.emplace_back(
            ExecutionLogicalTask {
                .loop = {.next = {}},
                .operation = std::move(operation),
                .receiver = std::nullopt,
                .parent = parent,
                .requested = false,
                .calls = {},
                .blocks = parent->blocks,
                .condition_observation = std::nullopt,
            }
        );
        const auto iterator = std::prev(tasks.end());
        children.push_back(iterator);
        iterator->operation->start(iterator->loop);
        ready.push_back({
            .task = std::addressof(*iterator),
            .continuation = std::exchange(iterator->loop.next, {}),
        });
    };
    for (auto& argument : arguments) {
        if (auto checked = step(origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        auto input = materialize(std::move(argument), origin);
        if (!input) {
            co_return std::unexpected(std::move(input.error()));
        }
        auto* descriptor = std::get_if<std::unique_ptr<ExecutionColdOperation>>(&*input);
        if (!descriptor || !*descriptor) {
            invariant_violation("composition owns an input that is not a cold operation");
        }
        start(consume(std::move(**descriptor), origin));
    }
    auto active = std::vector<bool>(children.size(), true);
    auto results = std::vector<std::optional<ExecutionValue>>(children.size());
    auto outcome = std::optional<ExecutionResult<ExecutionValue>>();
    auto fallback = std::optional<ExecutionFailure>();
    auto stopping = false;
    const auto stop_children = [&]() noexcept -> ExecutionResult<void> {
        if (stopping) {
            return {};
        }
        stopping = true;
        for (const auto child : children) {
            if (auto checked = step(origin); !checked) {
                return std::unexpected(std::move(checked.error()));
            }
            if (child->operation) {
                request_cancel(*child);
            }
        }
        return {};
    };
    const auto selected_value =
        [&](std::size_t position,
            ExecutionValue value) noexcept -> ExecutionResult<ExecutionValue> {
        if (action.selection == ExecutionSelectionResult::Direct) {
            return std::move(value);
        }
        if (auto checked = check_aggregate_size(*success, origin); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        if (auto checked = account_aggregate(2uz, origin); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        auto elements = std::vector<ExecutionValue>();
        elements.emplace_back(
            ConstantAtom {
                .type = values.builtin_type(BuiltinType::Usize),
                .value = IntegerConstant::from_parts(position, false),
            }
        );
        elements.push_back(std::move(value));
        return constructed_value(
            ExecutionAggregateValue {
                .type = *success,
                .elements = std::move(elements),
                .capacity = 0uz
            },
            origin
        );
    };
    for (;;) {
        if (auto checked = step(origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (cancel_requested()) {
            outcome.emplace(std::unexpected(cancelled()));
            if (auto checked = stop_children(); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            break;
        }
        auto positions = std::vector<std::size_t>();
        auto candidates = std::vector<ExecutionLogicalTask*>();
        for (auto index = 0uz; index < children.size(); ++index) {
            if (auto checked = step(origin); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            if (active[index]) {
                positions.push_back(index);
                candidates.push_back(std::addressof(*children[index]));
            }
        }
        if (candidates.empty()) {
            break;
        }
        const auto selection = co_await ReadySelection(*this, std::move(candidates), true);
        const auto selected = selection.first;
        if (selected == positions.size() || cancel_requested()) {
            outcome.emplace(std::unexpected(cancelled()));
            if (auto checked = stop_children(); !checked) {
                co_return std::unexpected(std::move(checked.error()));
            }
            break;
        }
        const auto position = positions[selected];
        auto& child = *children[position];
        auto result = child.operation->take_result();
        child.operation.reset();
        active[position] = false;
        if (!result && fatal(result.error())) {
            co_return std::unexpected(stop(std::move(result.error())));
        }
        switch (action.policy) {
            case AsyncCompositionPolicy::WhenAll:
                if (result) {
                    results[position].emplace(std::move(*result));
                } else if (std::holds_alternative<ExecutionSourceFailure>(result.error())) {
                    outcome.emplace(std::unexpected(std::move(result.error())));
                    if (auto checked = stop_children(); !checked) {
                        co_return std::unexpected(std::move(checked.error()));
                    }
                } else {
                    if (!fallback) {
                        fallback = std::move(result.error());
                    }
                    if (auto checked = stop_children(); !checked) {
                        co_return std::unexpected(std::move(checked.error()));
                    }
                }
                break;
            case AsyncCompositionPolicy::WhenAny:
                outcome.emplace(
                    result ? selected_value(position, std::move(*result))
                           : ExecutionResult<ExecutionValue>(
                                 std::unexpected(std::move(result.error()))
                             )
                );
                if (auto checked = stop_children(); !checked) {
                    co_return std::unexpected(std::move(checked.error()));
                }
                break;
            case AsyncCompositionPolicy::FirstSuccessful:
                if (result) {
                    outcome.emplace(selected_value(position, std::move(*result)));
                    if (auto checked = stop_children(); !checked) {
                        co_return std::unexpected(std::move(checked.error()));
                    }
                } else if (!fallback
                           || (!std::holds_alternative<ExecutionSourceFailure>(*fallback)
                               && std::holds_alternative<ExecutionSourceFailure>(result.error()))) {
                    fallback = std::move(result.error());
                }
                break;
        }
        if (outcome) {
            break;
        }
    }
    // Outward commitment ends selection. Drain-only cleanup discards source
    // outcomes and later requests while retaining every task through quiescence.
    // Match the native scope's pump and owner wake, including already consumed
    // groups; neither turn can replace the committed completion.
    if (!children.empty()) {
        co_await Parking(*this);
    }
    for (auto index = children.size(); index != 0uz; --index) {
        auto& child = *children[index - 1uz];
        if (auto checked = step(origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (!child.operation) {
            continue;
        }
        co_await Parking(*this, &child);
        const auto& result = child.operation->result();
        if (!result && fatal(result.error())) {
            co_return std::unexpected(stop(result.error()));
        }
        child.operation.reset();
    }
    if (!children.empty()) {
        co_await Parking(*this);
    }
    for (const auto child : children) {
        if (auto checked = step(origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        tasks.erase(child);
    }
    if (outcome) {
        co_return std::move(*outcome);
    }
    if (fallback) {
        co_return std::unexpected(std::move(*fallback));
    }
    if (action.policy != AsyncCompositionPolicy::WhenAll) {
        invariant_violation("competitive composition exhausted without an outcome");
    }
    if (results.empty()) {
        co_return ExecutionVoid {};
    }
    if (auto checked = check_aggregate_size(*success, origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    if (auto checked = account_aggregate(results.size(), origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    auto elements = std::vector<ExecutionValue>();
    elements.reserve(results.size());
    for (auto& result : results) {
        if (auto checked = step(origin); !checked) {
            co_return std::unexpected(std::move(checked.error()));
        }
        if (!result) {
            invariant_violation("successful join has an unfilled result slot");
        }
        elements.push_back(std::move(*result));
    }
    co_return constructed_value(
        ExecutionAggregateValue {
            .type = *success,
            .elements = std::move(elements),
            .capacity = 0uz
        },
        origin
    );
}
