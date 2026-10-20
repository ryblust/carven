module carven:semantic.evaluation.host.impl;

import :semantic.evaluation.executor;
import :semantic.evaluation.limits;
import :semantic.evaluation.value;
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
            co_return std::make_unique<ExecutionColdOperation>(
                result,
                ExecutionInputAction {
                    .mode = mode,
                    .limit = static_cast<std::size_t>(scalar(1).magnitude())
                },
                std::vector<ExecutionOperand>()
            );
        }
        case ProvidedFunction::WaitUntil:
            co_return std::make_unique<ExecutionColdOperation>(
                result,
                ExecutionTimerAction {.deadline = *scalar(0).as_signed(), .failure = std::nullopt},
                std::vector<ExecutionOperand>()
            );
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
          .limit = std::min(action.limit, maximum_constant_bytes),
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

SemanticExecutor::ExternalWait::ExternalWait(
    SemanticExecutor& executor,
    std::unique_ptr<ExecutionExternalOperation> operation,
    ProgramOriginID origin
) noexcept
    : executor(executor),
      operation(std::move(operation)),
      origin(origin),
      registration(std::nullopt),
      work(std::nullopt),
      result(std::nullopt),
      cancelled(false) {}

SemanticExecutor::ExternalWait::~ExternalWait() noexcept {
    if (registration) {
        executor.external_requests.erase(*registration);
    }
    operation->cancel();
}

auto SemanticExecutor::ExternalWait::await_ready() noexcept -> bool {
    cancelled = executor.cancel_requested();
    if (cancelled) {
        operation->cancel();
        return true;
    }
    result = operation->poll();
    return result.has_value();
}

auto SemanticExecutor::ExternalWait::suspend(
    ContinuationTaskLoop& loop,
    std::coroutine_handle<> continuation
) noexcept -> void {
    if (&loop != &executor.current->loop) {
        invariant_violation("external wait parked in another task");
    }
    work = ExecutionWork {.task = executor.current, .continuation = continuation};
    executor.external_requests.push_back(this);
    registration = std::prev(executor.external_requests.end());
}

auto SemanticExecutor::ExternalWait::await_resume() noexcept
    -> std::expected<ExecutionValue, ExecutionCallFailure> {
    if (cancelled) {
        return std::unexpected(ExecutionCallFailure {executor.cancelled(origin)});
    }
    if (!result) {
        invariant_violation("external wait resumed without completion");
    }
    return std::move(*result);
}

auto SemanticExecutor::poll_host() noexcept -> void {
    for (auto iterator = external_requests.begin(); iterator != external_requests.end();) {
        auto& waiting = **iterator;
        if (task_cancel_requested(*waiting.work->task)) {
            waiting.cancelled = true;
            waiting.operation->cancel();
        } else {
            waiting.result = waiting.operation->poll();
        }
        if (waiting.cancelled || waiting.result) {
            iterator = external_requests.erase(iterator);
            waiting.registration.reset();
            ready.push_back(*waiting.work);
        } else {
            ++iterator;
        }
    }

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
    auto deadline = timers.empty() ? std::nullopt : std::optional(timers.front()->deadline);
    const auto sources = external_requests.size() + !input_requests.empty() + !timers.empty();
    if (!external_requests.empty() && sources > 1uz) {
        // Independent native roots and input sources share bounded idle waits.
        const auto poll_deadline = host_monotonic_now() + 10'000'000;
        if (!deadline || poll_deadline < *deadline) {
            deadline = poll_deadline;
        }
    }
    for (auto* waiting : external_requests) {
        if (waiting->operation->wait(deadline)) {
            return true;
        }
    }
    if (!input_requests.empty()) {
        HostInputCursor::process().wait(deadline);
        return true;
    }
    if (timers.empty()) {
        return false;
    }
    host_monotonic_wait(*deadline);
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
    auto checked = constructed_value(std::move(payload), origin);
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
    if (auto checked = account_bytes(bytes.size(), origin); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    if (auto checked = account_aggregate(4uz, origin); !checked) {
        return std::unexpected(std::move(checked.error()));
    }
    auto result = std::vector<ExecutionValue>();
    result.emplace_back(
        ExecutionBytes {
            .type = fields[0],
            .element = values.builtin_type(BuiltinType::U8),
            .bytes = carven::runtime::Vector<std::uint8_t>::from_slice(
                carven::runtime::Slice<std::uint8_t>(bytes)
            )
        }
    );
    result.emplace_back(ConstantAtom {.type = fields[1], .value = BooleanConstant {.value = end}});
    result.emplace_back(
        ConstantAtom {.type = fields[2], .value = IntegerConstant::from_signed(system_error)}
    );
    result.emplace_back(
        ConstantAtom {.type = fields[3], .value = BooleanConstant {.value = limited}}
    );
    return constructed_value(
        ExecutionAggregateValue {
            .type = result_type,
            .elements = std::move(result),
            .capacity = 0uz
        },
        origin
    );
}
