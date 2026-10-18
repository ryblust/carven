module carven:semantic.evaluation.host.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.semir.type;
import :support.clock;
import :support.input;
import :support.invariant;
import std;

auto SemanticExecutor::provided_call(
    ProvidedFunction function,
    TypeID result,
    std::vector<ExecutionOperand> arguments,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    auto inputs = std::vector<ExecutionValue>();
    for (auto& argument : arguments) {
        auto input = materialize(std::move(argument), origin);
        if (!input) {
            co_return std::unexpected(std::move(input.error()));
        }
        inputs.push_back(std::move(*input));
    }
    const auto scalar = [&](std::size_t index) noexcept -> IntegerConstant {
        const auto atom = execution_atom(values, inputs.at(index));
        if (!atom || !std::holds_alternative<IntegerConstant>(atom->value)) {
            invariant_violation("provided scalar argument is not an integer");
        }
        return std::get<IntegerConstant>(atom->value);
    };
    switch (function) {
        case ProvidedFunction::MonotonicNow: {
            auto instant = host_clock(origin);
            if (!instant) {
                co_return std::unexpected(std::move(instant.error()));
            }
            co_return ConstantAtom {
                .type = result,
                .value = IntegerConstant::from_signed(*instant)
            };
        }
        case ProvidedFunction::ReadInput: {
            auto mode = HostInputMode::Chunk;
            switch (scalar(0).magnitude()) {
                case 0:  break;
                case 1:  mode = HostInputMode::Line; break;
                case 2:  mode = HostInputMode::All; break;
                default: invariant_violation("standard input selected an unknown read mode");
            }
            co_return std::make_unique<ExecutionColdOperation>(ExecutionColdOperation {
                .type = result,
                .action =
                    ExecutionInputAction {
                        .mode = mode,
                        .limit = static_cast<std::size_t>(scalar(1).magnitude())
                    },
                .arguments = {}
            });
        }
        case ProvidedFunction::WaitUntil:
            co_return std::make_unique<ExecutionColdOperation>(ExecutionColdOperation {
                .type = result,
                .action =
                    ExecutionTimerAction {
                        .deadline = *scalar(0).as_signed(),
                        .failure = std::nullopt
                    },
                .arguments = {}
            });
    }
    std::unreachable();
}

SemanticExecutor::TimerWait::TimerWait(SemanticExecutor& executor, std::int64_t deadline) noexcept
    : executor(executor),
      deadline(deadline),
      registration(std::nullopt),
      work(std::nullopt) {}

SemanticExecutor::TimerWait::~TimerWait() noexcept {
    if (registration) {
        executor.timers.erase(*registration);
    }
}

auto SemanticExecutor::TimerWait::await_ready() noexcept -> bool {
    cancelled = executor.cancel_requested();
    return cancelled || deadline <= host_monotonic_now();
}

auto SemanticExecutor::TimerWait::suspend(
    ContinuationTaskLoop& loop,
    std::coroutine_handle<> continuation
) noexcept -> void {
    if (&loop != &executor.current->loop) {
        invariant_violation("host timer parked in another task");
    }
    work = ExecutionWork {.task = executor.current, .continuation = continuation};
    const auto position = std::ranges::find_if(executor.timers, [&](const auto* waiting) noexcept {
        return waiting->deadline > deadline;
    });
    registration = executor.timers.insert(position, this);
}

auto SemanticExecutor::TimerWait::await_resume() const noexcept -> bool {
    return cancelled;
}

SemanticExecutor::InputWait::InputWait(
    SemanticExecutor& executor,
    ExecutionInputAction action,
    TypeID result_type,
    ProgramOriginID origin
) noexcept
    : executor(executor),
      result_type(result_type),
      origin(origin),
      user_limit(action.limit),
      request {
          .mode = action.mode,
          .limit = std::min(action.limit, maximum_constant_aggregate_elements),
          .scanned = 0uz
      },
      work(std::nullopt),
      result(std::nullopt),
      registration(std::nullopt) {}

SemanticExecutor::InputWait::~InputWait() noexcept {
    if (registration) {
        executor.input_requests.erase(*registration);
    }
}

auto SemanticExecutor::InputWait::await_ready() const noexcept -> bool {
    return false;
}

auto SemanticExecutor::InputWait::suspend(
    ContinuationTaskLoop& loop,
    std::coroutine_handle<> continuation
) noexcept -> void {
    if (&loop != &executor.current->loop) {
        invariant_violation("host input parked in another task");
    }
    work = ExecutionWork {.task = executor.current, .continuation = continuation};
    executor.input_requests.push_back(this);
    registration = std::prev(executor.input_requests.end());
}

auto SemanticExecutor::InputWait::await_resume() noexcept -> ExecutionResult<ExecutionValue> {
    if (!result) {
        return std::unexpected(executor.cancelled(origin));
    }
    return std::move(*result);
}

auto SemanticExecutor::task_cancel_requested(const ExecutionLogicalTask& source) noexcept -> bool {
    for (auto* task = &source; task; task = task->parent) {
        if (task->requested) {
            return true;
        }
    }
    return false;
}

auto SemanticExecutor::host_clock(ProgramOriginID origin) noexcept
    -> ExecutionResult<std::int64_t> {
    if (const auto rejection = context.host_effects_rejection()) {
        return std::unexpected(fail(origin, ExecutionReason::Admission, std::string(*rejection)));
    }
    return host_monotonic_now();
}

auto SemanticExecutor::poll_host() noexcept -> void {
    // Commit cancellation before expiry and input readiness, matching the native driver.
    for (auto it = timers.begin(); it != timers.end();) {
        auto& waiting = **it;
        if (task_cancel_requested(*waiting.work->task)) {
            it = timers.erase(it);
            waiting.registration.reset();
            waiting.cancelled = true;
            ready.push_back(*waiting.work);
        } else {
            ++it;
        }
    }
    if (!timers.empty()) {
        const auto instant = host_monotonic_now();
        while (!timers.empty() && timers.front()->deadline <= instant) {
            auto& waiting = *timers.front();
            timers.pop_front();
            waiting.registration.reset();
            ready.push_back(*waiting.work);
        }
    }
    for (auto it = input_requests.begin(); it != input_requests.end();) {
        auto& input = **it;
        if (task_cancel_requested(*input.work->task)) {
            it = input_requests.erase(it);
            input.registration.reset();
            ready.push_back(*input.work);
        } else {
            ++it;
        }
    }
    if (input_requests.empty()) {
        return;
    }
    auto& input = *input_requests.front();
    auto& cursor = HostInputCursor::process();
    auto result = cursor.ready(input.request);
    if (!result) {
        cursor.fill();
        if (auto filled = cursor.ready(input.request)) {
            result.emplace(*filled);
        }
    }
    if (result) {
        // Materialize while the shared cursor's bytes are borrowed. The completed
        // task owns its semantic result before another read can fill or consume.
        auto* interrupted = current;
        current = input.work->task;
        input.result.emplace(input_value(*result, input));
        current = interrupted;
        if (const auto* packet = std::get_if<HostInputPacket>(&*result);
            packet && input.result->has_value()) {
            cursor.consume(packet->bytes.size());
        }
        input_requests.pop_front();
        input.registration.reset();
        ready.push_back(*input.work);
    }
}

auto SemanticExecutor::wait_host() noexcept -> bool {
    if (!input_requests.empty()) {
        HostInputCursor::process().wait(
            timers.empty() ? std::nullopt : std::optional(timers.front()->deadline)
        );
        return true;
    }
    if (timers.empty()) {
        return false;
    }
    host_monotonic_wait(timers.front()->deadline);
    return true;
}

auto SemanticExecutor::cancelled(ProgramOriginID origin) const noexcept -> ExecutionFailure {
    return ExecutionCancelled {
        .origin = origin,
        .calls = current->calls,
        .blocks = current->blocks
    };
}

auto SemanticExecutor::source_failure(
    TypeID failure,
    ExecutionValue payload,
    ProgramOriginID origin
) noexcept -> ExecutionFailure {
    auto checked = bounded_value(std::move(payload), origin);
    if (!checked) {
        return std::move(checked.error());
    }
    return ExecutionSourceFailure {
        .type = failure,
        .payload = std::make_shared<const ExecutionValue>(std::move(*checked)),
        .origin = origin,
        .calls = current->calls,
        .blocks = current->blocks
    };
}

auto SemanticExecutor::host_operation(
    ExecutionColdOperation operation,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    if (cancel_requested()) {
        co_return std::unexpected(cancelled(origin));
    }
    if (const auto rejection = context.host_effects_rejection()) {
        co_return std::unexpected(
            fail(origin, ExecutionReason::Admission, std::string(*rejection))
        );
    }
    if (const auto* timer = std::get_if<ExecutionTimerAction>(&operation.action)) {
        if (co_await TimerWait(*this, timer->deadline)) {
            co_return std::unexpected(cancelled(origin));
        }
        if (timer->failure) {
            co_return std::unexpected(source_failure(
                *timer->failure,
                ExecutionAggregateValue {.type = *timer->failure, .elements = {}, .capacity = 0uz},
                origin
            ));
        }
        co_return ExecutionVoid {};
    }
    auto operation_type = type(operation.type, origin);
    if (!operation_type) {
        co_return std::unexpected(std::move(operation_type.error()));
    }
    const auto result_type =
        std::get<OperationTypeValue>(values.type_copy(*operation_type).value).success;
    co_return co_await InputWait(
        *this,
        std::get<ExecutionInputAction>(operation.action),
        result_type,
        origin
    );
}

auto SemanticExecutor::input_value(const HostInputResult& response, const InputWait& input) noexcept
    -> ExecutionResult<ExecutionValue> {
    const auto origin = input.origin;
    const auto result_type = input.result_type;
    const auto fields = std::get<TupleTypeValue>(values.type_copy(result_type).value).elements;
    auto bytes = std::span<const std::uint8_t>();
    auto end = false;
    auto system_error = std::int32_t {0};
    auto limited = false;
    if (const auto* packet = std::get_if<HostInputPacket>(&response)) {
        bytes = packet->bytes;
        end = packet->end;
    } else if (const auto* error = std::get_if<HostInputSystemError>(&response)) {
        system_error = error->code;
    } else {
        if (input.request.limit < input.user_limit) {
            return std::unexpected(
                fail(origin, ExecutionReason::Limit, "input exceeds the execution value size limit")
            );
        }
        limited = true;
    }
    if (auto checked = account_aggregate(bytes.size(), origin); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    if (auto checked = account_aggregate(4uz, origin); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    auto elements = std::vector<ExecutionValue>();
    elements.reserve(bytes.size());
    for (const auto byte : bytes) {
        if (auto checked = step(origin); !checked) {
            return std::unexpected(std::move(checked.error()));
        }
        elements.emplace_back(
            ConstantAtom {
                .type = values.builtin_type(BuiltinType::U8),
                .value = IntegerConstant::from_parts(byte, false)
            }
        );
    }
    auto result = std::vector<ExecutionValue>();
    result.emplace_back(
        ExecutionAggregateValue {
            .type = fields[0],
            .elements = std::move(elements),
            .capacity = bytes.size()
        }
    );
    result.emplace_back(ConstantAtom {.type = fields[1], .value = BooleanConstant {.value = end}});
    result.emplace_back(
        ConstantAtom {.type = fields[2], .value = IntegerConstant::from_signed(system_error)}
    );
    result.emplace_back(
        ConstantAtom {.type = fields[3], .value = BooleanConstant {.value = limited}}
    );
    return bounded_value(
        ExecutionAggregateValue {
            .type = result_type,
            .elements = std::move(result),
            .capacity = 0uz
        },
        origin
    );
}
