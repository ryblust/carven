module carven:semantic.evaluation.timeout.impl;

import :semantic.evaluation.executor;
import :semantic.semir.type;
import :support.invariant;
import std;

auto SemanticExecutor::timeout(ExecutionColdOperation operation, ProgramOriginID origin) noexcept
    -> ExecutionTask<ExecutionValue> {
    const auto action = std::get<ExecutionTimeoutAction>(operation.action);
    if (cancel_requested()) {
        co_return std::unexpected(cancelled(origin));
    }
    if (operation.arguments.size() != 2uz) {
        invariant_violation("timeout requires two operands");
    }
    auto duration = materialize(std::move(operation.arguments.back()), origin);
    if (!duration) {
        co_return std::unexpected(std::move(duration.error()));
    }
    const auto fact = read_fact(*duration, origin);
    if (!fact) {
        co_return std::unexpected(fact.error());
    }
    const auto* ticks = std::get_if<IntegerConstant>(&fact->value);
    if (!ticks) {
        invariant_violation("timeout duration is not signed integer ticks");
    }
    const auto fail_with = [&](TypeID failure) noexcept -> ExecutionFailure {
        return source_failure(
            failure,
            ExecutionAggregateValue {.type = failure, .elements = {}},
            origin
        );
    };
    if (ticks->negative() || ticks->magnitude() == 0u) {
        co_return std::unexpected(fail_with(action.timeout_failure));
    }
    auto sampled = host_clock(origin);
    if (!sampled) {
        co_return std::unexpected(std::move(sampled.error()));
    }
    const auto instant = *sampled;
    const auto delay = *ticks->as_signed();
    if (instant > std::numeric_limits<std::int64_t>::max() - delay) {
        co_return std::unexpected(fail_with(action.range_failure));
    }
    const auto success = visit_execution_type(
        values,
        operation.type,
        [](const auto& shape) noexcept -> ConstructionTypeRef {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, OperationTypeValue>
                          || std::same_as<Shape, ConstructionOperationTypeValue>) {
                return shape.success;
            } else {
                invariant_violation("timeout descriptor requires an operation type");
            }
        }
    );
    auto result = type(success, origin);
    if (!result) {
        co_return std::unexpected(std::move(result.error()));
    }
    auto timer = std::make_unique<ExecutionColdOperation>(ExecutionColdOperation {
        .type = operation.type,
        .action =
            ExecutionTimerAction {.deadline = instant + delay, .failure = action.timeout_failure},
        .arguments = {}
    });
    auto inputs = std::vector<ExecutionOperand>();
    inputs.push_back(std::move(operation.arguments.front()));
    inputs.emplace_back(ExecutionValue(std::move(timer)));
    co_return co_await compose(
        ExecutionColdOperation {
            .type = operation.type,
            .action =
                ExecutionCompositionAction {
                    .policy = AsyncCompositionPolicy::WhenAny,
                    .success = *result,
                    .selection = ExecutionSelectionResult::Direct
                },
            .arguments = std::move(inputs)
        },
        origin
    );
}
