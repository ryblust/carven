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
                ready.push_back(*std::exchange(current->receiver, std::nullopt));
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

auto SemanticExecutor::consume(ExecutionColdOperation operation, ProgramOriginID origin) noexcept
    -> ExecutionTask<ExecutionValue> {
    if (const auto* callable = std::get_if<CallableID>(&operation.action)) {
        co_return co_await call(*callable, std::move(operation.arguments), origin);
    }
    switch (std::get<AsyncIntrinsic>(operation.action)) {
        case AsyncIntrinsic::YieldOnce: co_await Parking(*this); co_return ExecutionVoid {};
        case AsyncIntrinsic::CancellationPoint:
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
                child.task->requested = true;
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
