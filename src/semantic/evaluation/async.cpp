module carven:semantic.evaluation.async.impl;

import :semantic.evaluation.executor;
import :support.invariant;
import std;

SemanticExecutor::~SemanticExecutor() noexcept {
    // Child ownership is flat. Destroy parked dependency frames before source memory.
    for (auto& task : tasks) {
        task.operation.reset();
    }
}

auto SemanticExecutor::fatal(const ExecutionFailure& failure) noexcept -> bool {
    return std::holds_alternative<ExecutionHalt>(failure)
        || std::holds_alternative<ExecutionDependencyFailure>(failure);
}

auto SemanticExecutor::stop(ExecutionFailure failure) noexcept -> ExecutionFailure {
    if (fatal(failure)) {
        if (!stopped) {
            stopped = failure;
        }
        return *stopped;
    }
    return failure;
}

auto SemanticExecutor::drive(ExecutionTask<ExecutionValue> operation) noexcept
    -> ExecutionResult<ExecutionValue> {
    if (!tasks.empty()) {
        invariant_violation("execution root was started more than once");
    }
    tasks.emplace_back(
        ExecutionLogicalTask {
            .loop = {.next = {}},
            .operation = std::nullopt,
            .receiver = std::nullopt,
            .parent = nullptr,
            .requested = false,
            .calls = {},
            .blocks = {},
            .condition_observation = std::nullopt,
        }
    );
    auto& root = tasks.back();
    root.operation.emplace(std::move(operation));
    root.operation->start(root.loop);
    ready.push_back({.task = &root, .continuation = std::exchange(root.loop.next, {})});
    while (!root.operation->done() && !stopped) {
        poll_cancellation();
        if (ready.empty()) {
            invariant_violation("unfinished execution has no runnable task");
        }
        const auto work = ready.front();
        ready.pop_front();
        current = work.task;
        current->loop.next = work.continuation;
        while (current->loop.next && !stopped) {
            current->loop.resume_one();
        }
        if (!stopped && current->operation->done()) {
            const auto& result = current->operation->result();
            if (!result && fatal(result.error())) {
                stop(result.error());
            } else if (current->receiver) {
                const auto receiver = *std::exchange(current->receiver, std::nullopt);
                if (const auto* work = std::get_if<ExecutionWork>(&receiver)) {
                    ready.push_back(*work);
                } else {
                    auto* registration = std::get<ExecutionReadyRegistration*>(receiver);
                    registration->task = nullptr;
                    registration->notify(
                        *registration,
                        !result && std::holds_alternative<ExecutionCancelled>(result.error())
                    );
                }
            }
        }
    }
    current = &root;
    auto result = stopped ? ExecutionResult<ExecutionValue>(std::unexpected(*stopped))
                          : root.operation->take_result();
    // Fatal execution abandons source cleanup; no budget-free source code runs here.
    ready.clear();
    for (auto& task : tasks) {
        task.operation.reset();
    }
    return result;
}

auto SemanticExecutor::invoke(
    CallableID callable,
    std::vector<ExecutionOperand> arguments,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    co_return drive(call(callable, std::move(arguments), origin));
}

auto SemanticExecutor::evaluate_root(const SemanticExpression& source) noexcept
    -> ExecutionTask<ExecutionValue> {
    co_return drive(root_expression(source));
}

auto SemanticExecutor::evaluate_body(ExecutionBody body) noexcept -> ExecutionTask<void> {
    auto result = drive(root_body(body));
    if (!result) {
        co_return std::unexpected(std::move(result.error()));
    }
    co_return {};
}

SemanticExecutor::Parking::Parking(
    SemanticExecutor& executor,
    ExecutionLogicalTask* dependency
) noexcept
    : executor(executor),
      dependency(dependency) {}

auto SemanticExecutor::Parking::await_ready() const noexcept -> bool {
    return dependency != nullptr && dependency->operation->done();
}

auto SemanticExecutor::Parking::await_resume() const noexcept -> void {}

SemanticExecutor::ReadySelection::ReadySelection(
    SemanticExecutor& executor,
    std::vector<ExecutionLogicalTask*> children,
    bool accept_cancellation
) noexcept
    : executor(executor),
      children(std::move(children)),
      registrations(),
      continuation(std::nullopt),
      winner(std::nullopt),
      cancelled(false),
      cancellation_task(accept_cancellation ? executor.current : nullptr),
      cancellation_registration(std::nullopt) {
    registrations.reserve(this->children.size());
    for (auto index = 0uz; index < this->children.size(); ++index) {
        registrations.push_back(
            {.task = nullptr,
             .receiver = this,
             .index = index,
             .notify = [](ExecutionReadyRegistration& registration,
                          bool cancelled) static noexcept {
                 auto& selection = *static_cast<ReadySelection*>(registration.receiver);
                 selection.commit(registration.index, cancelled);
                 selection.executor.ready.push_back(*selection.continuation);
             }}
        );
    }
}

SemanticExecutor::ReadySelection::~ReadySelection() noexcept {
    // Fatal root teardown destroys the waiter without running source cleanup.
    // Its borrowed child metadata remains alive while these edges are detached.
    detach();
}

auto SemanticExecutor::ReadySelection::detach() noexcept -> void {
    if (cancellation_registration) {
        executor.cancellation_waiters.erase(*cancellation_registration);
        cancellation_registration.reset();
    }
    for (auto& registration : registrations) {
        if (registration.task == nullptr) {
            continue;
        }
        auto& receiver = registration.task->receiver;
        const auto* selected =
            receiver ? std::get_if<ExecutionReadyRegistration*>(&*receiver) : nullptr;
        if (selected == nullptr || *selected != &registration) {
            invariant_violation("execution completion registration lost its receiver");
        }
        receiver.reset();
        registration.task = nullptr;
    }
}

auto SemanticExecutor::ReadySelection::commit(std::size_t index, bool value) noexcept -> void {
    if (winner) {
        invariant_violation("execution readiness selected more than one winner");
    }
    winner = index;
    cancelled = value;
    detach();
}

auto SemanticExecutor::ReadySelection::await_ready() noexcept -> bool {
    if (cancellation_task && executor.cancellation_requested()) {
        commit(children.size(), true);
        return true;
    }
    auto ready = std::optional<std::size_t>();
    for (auto index = 0uz; index < children.size(); ++index) {
        auto* child = children[index];
        if (child->receiver) {
            invariant_violation("execution child has more than one completion receiver");
        }
        registrations[index].task = child;
        child->receiver = &registrations[index];
        if (!ready && child->operation->done()) {
            ready = index;
        }
    }
    if (ready) {
        const auto& result = children[*ready]->operation->result();
        commit(*ready, !result && std::holds_alternative<ExecutionCancelled>(result.error()));
    }
    return ready.has_value();
}

auto SemanticExecutor::ReadySelection::suspend(
    ContinuationTaskLoop& loop,
    std::coroutine_handle<> handle
) noexcept -> void {
    if (&loop != &executor.current->loop) {
        invariant_violation("execution readiness parked in another task's loop");
    }
    continuation = ExecutionWork {.task = executor.current, .continuation = handle};
    if (cancellation_task) {
        executor.cancellation_waiters.push_front(this);
        cancellation_registration = executor.cancellation_waiters.begin();
    }
}

auto SemanticExecutor::ReadySelection::await_resume() const noexcept
    -> std::pair<std::size_t, bool> {
    if (!winner) {
        invariant_violation("execution readiness resumed without a winner");
    }
    return {*winner, cancelled};
}

auto SemanticExecutor::park(
    ContinuationTaskLoop& loop,
    std::coroutine_handle<> continuation,
    ExecutionLogicalTask* dependency
) noexcept -> void {
    if (&loop != &current->loop) {
        invariant_violation("execution parked in another logical task's loop");
    }
    const auto work = ExecutionWork {.task = current, .continuation = continuation};
    if (dependency != nullptr) {
        if (dependency->receiver) {
            invariant_violation("execution child has more than one completion receiver");
        }
        dependency->receiver = work;
    } else {
        ready.push_back(work);
    }
}

auto SemanticExecutor::cancellation_requested() const noexcept -> bool {
    for (auto* task = current; task != nullptr; task = task->parent) {
        if (task->requested) {
            return true;
        }
    }
    return false;
}

auto SemanticExecutor::request_cancel(ExecutionLogicalTask& task) noexcept -> void {
    if (!task.requested) {
        task.requested = true;
        cancellation_dirty = true;
    }
}

auto SemanticExecutor::poll_cancellation() noexcept -> void {
    if (!std::exchange(cancellation_dirty, false)) {
        return;
    }
    for (auto iterator = cancellation_waiters.begin(); iterator != cancellation_waiters.end();) {
        auto& selection = **iterator++;
        for (auto* task = selection.cancellation_task; task != nullptr; task = task->parent) {
            if (task->requested) {
                selection.commit(selection.children.size(), true);
                ready.push_back(*selection.continuation);
                break;
            }
        }
    }
}

auto SemanticExecutor::consume(ExecutionColdOperation operation, ProgramOriginID origin) noexcept
    -> ExecutionTask<ExecutionValue> {
    if (const auto* callable = std::get_if<CallableID>(&operation.action)) {
        co_return co_await call(*callable, std::move(operation.arguments), origin);
    }
    if (std::holds_alternative<ExecutionCompositionAction>(operation.action)) {
        co_return co_await compose(std::move(operation), origin);
    }
    if (const auto* timeout = std::get_if<ExecutionTimeoutAction>(&operation.action)) {
        if (cancellation_requested()) {
            co_return std::unexpected(
                ExecutionFailure {ExecutionCancelled {
                    .origin = origin,
                    .calls = current->calls,
                    .blocks = current->blocks,
                }}
            );
        }
        if (operation.arguments.size() != 2uz) {
            invariant_violation("timeout requires its operation and duration operands");
        }
        auto duration = materialize(std::move(operation.arguments.back()), origin);
        if (!duration) {
            co_return std::unexpected(std::move(duration.error()));
        }
        const auto fact = read_fact(*duration, origin);
        if (!fact) {
            co_return std::unexpected(std::move(fact.error()));
        }
        const auto* ticks = std::get_if<IntegerConstant>(&fact->value);
        if (!ticks) {
            invariant_violation("timeout duration is not signed integer ticks");
        }
        if (ticks->negative() || ticks->magnitude() == 0u) {
            co_return std::unexpected(
                ExecutionFailure {ExecutionSourceFailure {
                    .type = timeout->timeout_failure,
                    .payload = std::make_shared<const ExecutionValue>(ExecutionAggregateValue {
                        .type = timeout->timeout_failure,
                        .elements = {},
                    }),
                    .origin = origin,
                    .calls = current->calls,
                    .blocks = current->blocks,
                }}
            );
        }
        co_return std::unexpected(
            fail(origin, ExecutionReason::Admission, "positive timeout requires a native clock")
        );
    }
    switch (std::get<AsyncIntrinsic>(operation.action)) {
        case AsyncIntrinsic::YieldOnce: co_await Parking(*this); co_return ExecutionVoid {};
        case AsyncIntrinsic::CancelPoint:
            if (cancellation_requested()) {
                co_return std::unexpected(
                    ExecutionFailure {ExecutionCancelled {
                        .origin = origin,
                        .calls = current->calls,
                        .blocks = current->blocks,
                    }}
                );
            }
            co_return ExecutionVoid {};
        case AsyncIntrinsic::CancelChild:
        case AsyncIntrinsic::CancellationRequested:
        case AsyncIntrinsic::WaitReady:
        case AsyncIntrinsic::CloseChild:
            invariant_violation("immediate async intrinsic stored as a cold operation");
    }
    std::unreachable();
}

auto SemanticExecutor::start_child(
    ExecutionFrame& frame,
    const SemAsyncLet& child,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionCompletion> {
    auto evaluated = co_await value(frame, child.initializer);
    if (!evaluated) {
        co_return std::unexpected(std::move(evaluated.error()));
    }
    auto* descriptor = std::get_if<std::unique_ptr<ExecutionColdOperation>>(&*evaluated);
    if (descriptor == nullptr || !*descriptor) {
        co_return std::unexpected(fail(
            origin,
            ExecutionReason::Evaluation,
            "async let requires an unconsumed cold operation"
        ));
    }
    auto owned = std::move(*descriptor);
    tasks.emplace_back(
        ExecutionLogicalTask {
            .loop = {.next = {}},
            .operation = std::nullopt,
            .receiver = std::nullopt,
            .parent = current,
            .requested = false,
            .calls = {},
            .blocks = current->blocks,
            .condition_observation = std::nullopt,
        }
    );
    auto& task = tasks.back();
    task.operation.emplace(consume(std::move(*owned), origin));
    task.operation->start(task.loop);
    frame.slots.at(child.child.index()) = ExecutionChild {.task = &task};
    frame.children.push_back({
        .lifetime = frame.body->binding_lifetime(child.child),
        .binding = child.child,
        .task = std::prev(tasks.end()),
    });
    ready.push_back({.task = &task, .continuation = std::exchange(task.loop.next, {})});
    co_return ExecutionCompletion {.flow = ExecutionFlow::Normal, .value = ExecutionVoid {}};
}

auto SemanticExecutor::observe_child(
    ExecutionFrame& frame,
    LocalBindingID binding,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto* child = std::get_if<ExecutionChild>(&frame.slots.at(binding.index()));
    if (child == nullptr || !child->task->operation) {
        co_return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "await requires an unconsumed lexical child")
        );
    }
    auto& task = *child->task;
    co_await Parking(*this, &task);
    auto result = task.operation->take_result();
    task.operation.reset();
    // The registration remains until lexical closure, even after observation.
    co_return result;
}

auto SemanticExecutor::wait_ready(
    ExecutionFrame& frame,
    const SemWaitReady& operation,
    const SemanticExpression& source
) noexcept -> ExecutionTask<ExecutionValue> {
    auto children = std::vector<ExecutionLogicalTask*>();
    for (const auto binding : operation.children) {
        const auto* child = std::get_if<ExecutionChild>(&frame.slots.at(binding.index()));
        if (!child
            || !child->task->operation
            || child->task->parent != current
            || child->task->receiver) {
            co_return std::unexpected(fail(
                source.origin,
                ExecutionReason::Evaluation,
                "wait_ready requires distinct unconsumed lexical children of its owner"
            ));
        }
        children.push_back(child->task);
    }
    if (children.empty()) {
        invariant_violation("execution readiness has no candidates");
    }
    const auto [index, cancelled] = co_await ReadySelection(*this, std::move(children));
    auto result_type = type(source.type.construction(), source.origin);
    if (!result_type) {
        co_return std::unexpected(std::move(result_type.error()));
    }
    if (auto checked = check_aggregate_size(*result_type, source.origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    if (auto checked = account_aggregate(2uz, source.origin); !checked) {
        co_return std::unexpected(std::move(checked.error()));
    }
    auto elements = std::vector<ExecutionValue>();
    elements.emplace_back(
        ConstantAtom {
            .type = values.builtin_type(BuiltinType::Usize),
            .value = IntegerConstant::from_parts(index, false)
        }
    );
    elements.emplace_back(
        ConstantAtom {
            .type = values.builtin_type(BuiltinType::Bool),
            .value = BooleanConstant {.value = cancelled}
        }
    );
    co_return ExecutionAggregateValue {.type = *result_type, .elements = std::move(elements)};
}

auto SemanticExecutor::close_child(
    ExecutionFrame& frame,
    LocalBindingID binding,
    ProgramOriginID origin
) noexcept -> ExecutionTask<ExecutionValue> {
    const auto* child = std::get_if<ExecutionChild>(&frame.slots.at(binding.index()));
    if (!child || child->task->parent != current) {
        co_return std::unexpected(
            fail(origin, ExecutionReason::Evaluation, "close requires a lexical child of its owner")
        );
    }
    auto& task = *child->task;
    if (!task.operation) {
        co_return ExecutionVoid {};
    }
    request_cancel(task);
    co_await Parking(*this, &task);
    const auto& result = task.operation->result();
    if (!result && fatal(result.error())) {
        co_return std::unexpected(stop(result.error()));
    }
    task.operation.reset();
    co_return ExecutionVoid {};
}

auto SemanticExecutor::close_scope(
    ExecutionFrame& frame,
    LifetimeRegionID lifetime,
    bool cancel
) noexcept -> ExecutionTask<void> {
    if (!frame.body || !frame.body->has_children(lifetime)) {
        co_return {};
    }
    if (cancel) {
        for (const auto& child : frame.children) {
            if (child.lifetime == lifetime && child.task->operation) {
                request_cancel(*child.task);
            }
        }
    }
    // These two FIFO turns correspond to the runtime's pump and owner wake.
    co_await Parking(*this);
    for (auto index = frame.children.size(); index != 0; --index) {
        const auto child = frame.children[index - 1];
        if (child.lifetime != lifetime || !child.task->operation) {
            continue;
        }
        co_await Parking(*this, std::addressof(*child.task));
        const auto& result = child.task->operation->result();
        if (!result && fatal(result.error())) {
            co_return std::unexpected(stop(result.error()));
        }
        // Unobserved source failures and cancellation are discarded by closure.
        child.task->operation.reset();
    }
    co_await Parking(*this);
    std::erase_if(frame.children, [&](const auto& child) noexcept {
        if (child.lifetime != lifetime) {
            return false;
        }
        // The terminal task has no queued work or live descendants. End the
        // lexical slot's borrow before reclaiming its metadata.
        release(frame, child.binding.index());
        tasks.erase(child.task);
        return true;
    });
    co_return {};
}

auto SemanticExecutor::close_region(
    ExecutionFrame& frame,
    LifetimeRegionID lifetime,
    ExecutionResult<ExecutionCompletion> result
) noexcept -> ExecutionTask<ExecutionCompletion> {
    if (!result && fatal(result.error())) {
        co_return std::unexpected(stop(std::move(result.error())));
    }
    auto closed = co_await close_scope(frame, lifetime, !result);
    if (!closed) {
        co_return std::unexpected(std::move(closed.error()));
    }
    release_region(frame, lifetime);
    co_return result;
}
