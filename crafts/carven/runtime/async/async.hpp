#pragma once

#include "completion.hpp"
#include "../trap.hpp"
#include <coroutine>
#include <cstddef>
#include <cstdio>
#include <exception>
#include <new>
#include <type_traits>
#include <utility>
#include <variant>

namespace carven::runtime::async {

class Driver;
struct TaskContext;
class OperationState;
class ChildScope;
class ResumeContinuation;
class YieldAwaiter;
template<typename Result, typename... Failures>
class Operation;
template<typename Result, typename... Failures>
class Child;
template<typename Result, typename... Failures, typename Pump = std::nullptr_t>
auto drive_root(Operation<Result, Failures...>&& operation, Pump* pump = nullptr) noexcept
    -> Completion<Result, Failures...>;
enum class ClosingPolicy { CloseOnly, RequestCancelAndClose };

namespace detail {
inline auto require_protocol(bool condition) noexcept -> void {
    if (!condition) {
        std::terminate();
    }
}

struct WorkNode final {
    WorkNode* next;
    TaskContext* task;
    void* argument;
    void (*invoke)(void*) noexcept;
};

inline auto resume_node(std::coroutine_handle<> handle, TaskContext& task) noexcept -> WorkNode {
    return WorkNode {
        .next = nullptr,
        .task = &task,
        .argument = handle.address(),
        .invoke = [](void* address) noexcept {
            const auto frame = std::coroutine_handle<>::from_address(address);
            // Native transfer can destroy this frame before resume returns.
            frame.resume();
        }
    };
}

inline thread_local TaskContext* current_task = nullptr;

} // namespace detail

// Logical cancellation identity is separate from an activation's lifetime gate.
struct TaskContext final {
    Driver* driver;
    TaskContext* parent;
    bool requested;
    auto cancellation_requested() const noexcept -> bool;
};

inline auto TaskContext::cancellation_requested() const noexcept -> bool {
    for (auto* task = this; task != nullptr; task = task->parent) {
        if (task->requested) {
            return true;
        }
    }
    return false;
}

class Driver final {
    // The driver stays on its constructing thread and borrows the TLS slot
    // used by synchronous context queries.
    TaskContext*& task_slot;
    detail::WorkNode* first = nullptr;
    detail::WorkNode* last = nullptr;
    bool dispatching = false;
    friend class ResumeContinuation;
    friend class OperationState;
    friend class ChildScope;
    friend class YieldAwaiter;
    template<typename T, typename... E>
    friend class Child;
    template<typename T, typename... E, typename Pump>
    friend auto drive_root(Operation<T, E...>&&, Pump*) noexcept -> Completion<T, E...>;
    auto enqueue(detail::WorkNode& node) noexcept -> void;

public:
    Driver() noexcept;
    Driver(const Driver&) = delete;
    Driver(Driver&&) = delete;
    auto operator=(const Driver&) -> Driver& = delete;
    auto operator=(Driver&&) -> Driver& = delete;
    ~Driver() noexcept;
    auto dispatch_one() noexcept -> bool;
};

inline Driver::Driver() noexcept
    : task_slot(detail::current_task) {}

inline Driver::~Driver() noexcept {
    detail::require_protocol(first == nullptr && !dispatching);
}

inline auto Driver::enqueue(detail::WorkNode& node) noexcept -> void {
    detail::require_protocol(node.invoke != nullptr);
    node.next = nullptr;
    if (last != nullptr) {
        last->next = &node;
    } else {
        first = &node;
    }
    last = &node;
}

inline auto Driver::dispatch_one() noexcept -> bool {
    detail::require_protocol(!dispatching);
    if (first == nullptr) {
        return false;
    }
    auto* node = first;
    first = node->next;
    if (first == nullptr) {
        last = nullptr;
    }
    // The action may reclaim its node. Copy all dispatch data before invoking it.
    const auto invoke = node->invoke;
    auto* argument = node->argument;
    auto* saved_task = task_slot;
    task_slot = node->task;
    dispatching = true;
    invoke(argument);
    dispatching = false;
    task_slot = saved_task;
    return true;
}

// A provider retains this continuation until it has committed its terminal
// result and released external reachability. Enqueue resumes through the driver.
class ResumeContinuation final {
    detail::WorkNode
        node {.next = nullptr, .task = nullptr, .argument = nullptr, .invoke = nullptr};

public:
    ResumeContinuation() noexcept = default;
    ResumeContinuation(const ResumeContinuation&) = delete;
    ResumeContinuation(ResumeContinuation&&) = delete;
    auto operator=(const ResumeContinuation&) -> ResumeContinuation& = delete;
    auto operator=(ResumeContinuation&&) -> ResumeContinuation& = delete;

    template<typename Promise>
    auto bind(std::coroutine_handle<Promise> handle) noexcept -> void {
        detail::require_protocol(!bound());
        node = detail::resume_node(handle, *handle.promise().state.task);
    }

    auto bound() const noexcept -> bool { return node.task != nullptr; }

    auto cancellation_requested() const noexcept -> bool {
        detail::require_protocol(bound());
        return node.task->cancellation_requested();
    }

    auto enqueue(Driver& driver) noexcept -> void {
        detail::require_protocol(bound() && node.task->driver == &driver);
        driver.enqueue(node);
    }
};

class OperationState final {
public:
    TaskContext* task = nullptr;
    std::variant<std::monostate, std::coroutine_handle<>, detail::WorkNode*> waiter;

    auto bind(TaskContext& context) noexcept -> void;
    auto publish() noexcept -> std::coroutine_handle<>;
    auto wait(std::coroutine_handle<> continuation) noexcept -> void;
    auto wait(detail::WorkNode& continuation) noexcept -> void;
};

inline auto OperationState::bind(TaskContext& context) noexcept -> void {
    detail::require_protocol(task == nullptr);
    task = &context;
}

inline auto OperationState::publish() noexcept -> std::coroutine_handle<> {
    detail::require_protocol(task != nullptr);
    if (const auto* handle = std::get_if<std::coroutine_handle<>>(&waiter)) {
        const auto continuation = *handle;
        waiter.emplace<std::monostate>();
        return continuation;
    }
    if (const auto* work = std::get_if<detail::WorkNode*>(&waiter)) {
        auto* continuation = *work;
        waiter.emplace<std::monostate>();
        task->driver->enqueue(*continuation);
    }
    return std::noop_coroutine();
}

inline auto OperationState::wait(std::coroutine_handle<> continuation) noexcept -> void {
    detail::require_protocol(std::holds_alternative<std::monostate>(waiter));
    waiter = continuation;
}

inline auto OperationState::wait(detail::WorkNode& continuation) noexcept -> void {
    detail::require_protocol(std::holds_alternative<std::monostate>(waiter));
    waiter = &continuation;
}

namespace detail {
inline auto destroy(std::coroutine_handle<> handle, const OperationState& state) noexcept -> void {
    require_protocol(state.task == nullptr || handle.done());
    require_protocol(std::holds_alternative<std::monostate>(state.waiter));
    handle.destroy();
}

} // namespace detail

class CurrentActivationAwaiter final {
    OperationState* activation = nullptr;

public:
    auto await_ready() const noexcept -> bool { return false; }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
        activation = &handle.promise().state;
        return false;
    }

    auto await_resume() const noexcept -> OperationState& { return *activation; }
};

inline auto current_activation() noexcept -> CurrentActivationAwaiter {
    return {};
}

inline auto cancellation_requested() noexcept -> bool {
    return detail::current_task != nullptr && detail::current_task->cancellation_requested();
}

template<typename Result, typename... Failures>
class Operation final {
public:
    class promise_type final {
    public:
        OperationState state;
        Completion<Result, Failures...> completion;

        promise_type() noexcept = default;

        static auto operator new(std::size_t size) noexcept -> void* {
            auto* storage = ::operator new(size, std::nothrow);
            if (storage == nullptr) {
                std::terminate();
            }
            return storage;
        }

        static auto operator delete(void* storage, std::size_t) noexcept -> void {
            ::operator delete(storage);
        }

        auto get_return_object() noexcept -> Operation {
            return Operation(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        auto initial_suspend() const noexcept -> std::suspend_always { return {}; }

        class FinalAwaiter final {
        public:
            auto await_ready() const noexcept -> bool { return false; }

            auto await_suspend(std::coroutine_handle<promise_type> handle) const noexcept
                -> std::coroutine_handle<> {
                detail::require_protocol(handle.done());
                return handle.promise().state.publish();
            }

            auto await_resume() const noexcept -> void {}
        };

        auto final_suspend() const noexcept -> FinalAwaiter { return {}; }

        auto unhandled_exception() const noexcept -> void { std::terminate(); }

        auto return_value(Completion<Result, Failures...> result) noexcept -> void {
            detail::require_protocol(completion.empty() && !result.empty());
            completion.initialize(std::move(result));
        }

        auto take_completion() noexcept -> Completion<Result, Failures...> {
            detail::require_protocol(!completion.empty());
            return Completion<Result, Failures...>(std::move(completion));
        }
    };

    using Handle = std::coroutine_handle<promise_type>;

private:
    Handle frame;

    explicit Operation(Handle handle) noexcept
        : frame(handle) {}
    template<typename T, typename... E>
    friend class Child;
    template<typename T, typename... E, typename Pump>
    friend auto drive_root(Operation<T, E...>&&, Pump*) noexcept -> Completion<T, E...>;

    auto release() noexcept -> Handle {
        detail::require_protocol(frame != nullptr);
        return std::exchange(frame, nullptr);
    }

public:
    Operation(const Operation&) = delete;

    Operation(Operation&& other) noexcept
        : frame(std::exchange(other.frame, nullptr)) {}

    auto operator=(const Operation&) -> Operation& = delete;
    auto operator=(Operation&&) -> Operation& = delete;

    ~Operation() noexcept {
        if (frame != nullptr) {
            detail::destroy(frame, frame.promise().state);
        }
    }

    // The Operation outlives suspension and owns the borrowed frame.
    class Awaiter final {
        Handle frame;

    public:
        explicit Awaiter(Handle handle) noexcept
            : frame(handle) {}

        Awaiter(const Awaiter&) = delete;
        Awaiter(Awaiter&&) = delete;
        auto operator=(const Awaiter&) -> Awaiter& = delete;
        auto operator=(Awaiter&&) -> Awaiter& = delete;

        ~Awaiter() noexcept = default;

        auto await_ready() const noexcept -> bool { return false; }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept
            -> std::coroutine_handle<> {
            auto& caller = handle.promise().state;
            auto& state = frame.promise().state;
            state.wait(handle);
            state.bind(*caller.task);
            return frame;
        }

        auto await_resume() noexcept -> Completion<Result, Failures...> {
            auto& promise = frame.promise();
            detail::require_protocol(frame.done());
            return promise.take_completion();
        }
    };

    auto operator co_await() && noexcept -> Awaiter { return Awaiter(frame); }
};

namespace detail {
struct ChildSlot final {
    ChildSlot* next;
    ChildScope* scope;
    std::coroutine_handle<> frame;
    OperationState* operation;
    void* child;
    void (*discard)(void*) noexcept;
};
} // namespace detail

class ChildScope final {
    OperationState* owner;
    detail::ChildSlot* children = nullptr;
    detail::WorkNode
        pump {.next = nullptr, .task = nullptr, .argument = nullptr, .invoke = nullptr};
    detail::WorkNode* continuation = nullptr;
    enum class Phase { Open, Closing, Closed };
    Phase phase = Phase::Open;
    template<typename T, typename... E>
    friend class Child;

    auto register_child(detail::ChildSlot& slot) noexcept -> void;
    auto drain() noexcept -> void;

public:
    explicit ChildScope(OperationState& activation) noexcept;
    ChildScope(const ChildScope&) = delete;
    ChildScope(ChildScope&&) = delete;
    auto operator=(const ChildScope&) -> ChildScope& = delete;
    auto operator=(ChildScope&&) -> ChildScope& = delete;
    ~ChildScope() noexcept;

    template<typename T, typename... E>
    auto start(Operation<T, E...>&& operation) noexcept -> Child<T, E...> {
        return Child<T, E...>(*this, std::move(operation));
    }

    class CloseAwaiter final {
        ChildScope* scope;
        ClosingPolicy policy;

        detail::WorkNode
            wake {.next = nullptr, .task = nullptr, .argument = nullptr, .invoke = nullptr};

    public:
        CloseAwaiter(ChildScope& value, ClosingPolicy selected) noexcept;
        CloseAwaiter(const CloseAwaiter&) = delete;
        CloseAwaiter(CloseAwaiter&&) = delete;
        auto operator=(const CloseAwaiter&) -> CloseAwaiter& = delete;
        auto operator=(CloseAwaiter&&) -> CloseAwaiter& = delete;
        auto await_ready() const noexcept -> bool;
        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> void;
        auto await_resume() const noexcept -> void;
    };

    auto close(ClosingPolicy selected) noexcept -> CloseAwaiter;
};

inline auto ChildScope::register_child(detail::ChildSlot& slot) noexcept -> void {
    detail::require_protocol(phase == Phase::Open);
    slot.next = children;
    slot.scope = this;
    children = &slot;
}

inline auto ChildScope::drain() noexcept -> void {
    while (children != nullptr) {
        auto* slot = children;
        if (slot->frame != nullptr && !slot->frame.done()) {
            slot->operation->wait(pump);
            return;
        }
        children = slot->next;
        slot->scope = nullptr;
        slot->next = nullptr;
        if (slot->frame != nullptr) {
            slot->discard(slot->child);
        }
    }
    phase = Phase::Closed;
    owner->task->driver->enqueue(*std::exchange(continuation, nullptr));
}

inline ChildScope::ChildScope(OperationState& activation) noexcept
    : owner(&activation) {
    pump.task = owner->task;
    pump.argument = this;
    pump.invoke = [](void* scope) noexcept {
        static_cast<ChildScope*>(scope)->drain();
    };
}

inline ChildScope::~ChildScope() noexcept {
    detail::require_protocol(
        phase == Phase::Closed && children == nullptr && continuation == nullptr
    );
}

inline auto ChildScope::close(ClosingPolicy selected) noexcept -> CloseAwaiter {
    return CloseAwaiter(*this, selected);
}

inline ChildScope::CloseAwaiter::CloseAwaiter(ChildScope& value, ClosingPolicy selected) noexcept
    : scope(&value),
      policy(selected) {}

inline auto ChildScope::CloseAwaiter::await_ready() const noexcept -> bool {
    return scope->phase == Phase::Closed;
}

template<typename Promise>
inline auto ChildScope::CloseAwaiter::await_suspend(std::coroutine_handle<Promise> handle) noexcept
    -> void {
    auto& activation = handle.promise().state;
    detail::require_protocol(&activation == scope->owner && scope->phase == Phase::Open);
    scope->phase = Phase::Closing;
    if (policy == ClosingPolicy::RequestCancelAndClose) {
        for (auto* child = scope->children; child != nullptr; child = child->next) {
            if (child->frame != nullptr) {
                child->operation->task->requested = true;
            }
        }
    }
    wake = detail::resume_node(handle, *activation.task);
    scope->continuation = &wake;
    scope->owner->task->driver->enqueue(scope->pump);
}

inline auto ChildScope::CloseAwaiter::await_resume() const noexcept -> void {
    detail::require_protocol(scope->phase == Phase::Closed);
}

template<typename Result, typename... Failures>
class Child final {
    using Handle = typename Operation<Result, Failures...>::Handle;
    detail::ChildSlot slot;
    TaskContext task;
    detail::WorkNode startup;
    friend class ChildScope;
    template<typename T, typename... E>
    friend auto cancel(Child<T, E...>&) noexcept -> void;

    Child(ChildScope& scope, Operation<Result, Failures...>&& operation) noexcept
        : slot {
              .next = nullptr,
              .scope = nullptr,
              .frame = operation.release(),
              .operation = nullptr,
              .child = this,
              .discard = [](void* child) noexcept { static_cast<Child*>(child)->discard(); }
          },
          task {
              .driver = scope.owner->task->driver,
              .parent = scope.owner->task,
              .requested = false
          },
          startup(detail::resume_node(slot.frame, task)) {
        const auto frame = frame_handle();
        slot.operation = &frame.promise().state;
        scope.register_child(slot);
        frame.promise().state.bind(task);
        task.driver->enqueue(startup);
    }

    auto frame_handle() const noexcept -> Handle {
        return Handle::from_address(slot.frame.address());
    }

    auto discard() noexcept -> void {
        const auto frame = frame_handle();
        detail::destroy(frame, frame.promise().state);
        slot.frame = nullptr;
        slot.operation = nullptr;
    }

public:
    Child(const Child&) = delete;
    Child(Child&&) = delete;
    auto operator=(const Child&) -> Child& = delete;
    auto operator=(Child&&) -> Child& = delete;

    ~Child() noexcept { detail::require_protocol(slot.frame == nullptr && slot.scope == nullptr); }

    class ObserveAwaiter final {
        Child* child;

        detail::WorkNode
            wake {.next = nullptr, .task = nullptr, .argument = nullptr, .invoke = nullptr};

    public:
        explicit ObserveAwaiter(Child& value) noexcept
            : child(&value) {
            detail::require_protocol(child->slot.frame != nullptr && child->slot.scope != nullptr);
        }

        ObserveAwaiter(const ObserveAwaiter&) = delete;
        ObserveAwaiter(ObserveAwaiter&&) = delete;
        auto operator=(const ObserveAwaiter&) -> ObserveAwaiter& = delete;
        auto operator=(ObserveAwaiter&&) -> ObserveAwaiter& = delete;

        auto await_ready() const noexcept -> bool {
            detail::require_protocol(child->slot.frame != nullptr);
            return child->slot.frame.done();
        }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> void {
            auto& caller = handle.promise().state;
            detail::require_protocol(
                child->slot.scope != nullptr && child->slot.scope->owner == &caller
            );
            wake = detail::resume_node(handle, *caller.task);
            child->slot.operation->wait(wake);
        }

        auto await_resume() noexcept -> Completion<Result, Failures...> {
            detail::require_protocol(child->slot.frame != nullptr && child->slot.frame.done());
            auto completion = child->frame_handle().promise().take_completion();
            child->discard();
            return completion;
        }
    };

    auto observe() & noexcept -> ObserveAwaiter { return ObserveAwaiter(*this); }
};

template<typename Result, typename... Failures>
inline auto cancel(Child<Result, Failures...>& child) noexcept -> void {
    detail::require_protocol(child.slot.frame != nullptr && child.slot.scope != nullptr);
    // Recording intent is immediate; descendants inspect ancestry iteratively.
    child.task.requested = true;
}

class CancellationPointAwaiter final {
public:
    auto await_ready() const noexcept -> bool { return true; }

    auto await_suspend(std::coroutine_handle<>) const noexcept -> void {}

    auto await_resume() const noexcept -> Completion<void> {
        return cancellation_requested() ? Completion<void>::cancelled()
                                        : Completion<void>::success();
    }
};

class YieldAwaiter final {
    detail::WorkNode
        resume {.next = nullptr, .task = nullptr, .argument = nullptr, .invoke = nullptr};

public:
    YieldAwaiter() noexcept = default;
    YieldAwaiter(const YieldAwaiter&) = delete;
    YieldAwaiter(YieldAwaiter&&) = delete;
    auto operator=(const YieldAwaiter&) -> YieldAwaiter& = delete;
    auto operator=(YieldAwaiter&&) -> YieldAwaiter& = delete;

    auto await_ready() const noexcept -> bool { return false; }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> void {
        auto& state = handle.promise().state;
        resume = detail::resume_node(handle, *state.task);
        state.task->driver->enqueue(resume);
    }

    auto await_resume() const noexcept -> Completion<void> { return Completion<void>::success(); }
};

inline auto await_cancellation_point() noexcept -> CancellationPointAwaiter {
    return {};
}

inline auto await_yield_once() noexcept -> YieldAwaiter {
    return {};
}

inline auto cancellation_point() noexcept -> Operation<void> {
    co_return co_await await_cancellation_point();
}

inline auto yield_once() noexcept -> Operation<void> {
    co_return co_await await_yield_once();
}

// A native pump polls external events into this driver and waits only when it is idle.
// Its callbacks retain registration state through quiescence and never resume user code.
template<typename Result, typename... Failures, typename Pump>
inline auto drive_root(Operation<Result, Failures...>&& operation, Pump* pump) noexcept
    -> Completion<Result, Failures...> {
    detail::require_protocol(detail::current_task == nullptr);
    auto driver = Driver();
    auto task = TaskContext {.driver = &driver, .parent = nullptr, .requested = false};
    auto frame = operation.release();
    auto& state = frame.promise().state;
    state.bind(task);
    auto startup = detail::resume_node(frame, task);
    driver.enqueue(startup);
    while (!frame.done()) {
        if constexpr (!std::is_same_v<Pump, std::nullptr_t>) {
            detail::require_protocol(pump != nullptr);
            pump->poll(driver);
        }
        const auto progressed = driver.dispatch_one();
        if constexpr (std::is_same_v<Pump, std::nullptr_t>) {
            detail::require_protocol(progressed);
        } else if (!progressed) {
            const auto pending = pump->wait();
            detail::require_protocol(pending);
        }
    }
    auto completion = frame.promise().take_completion();
    detail::destroy(frame, state);
    return completion;
}

inline auto report_entry_cancelled(SourceSite site) noexcept -> void {
    carven::runtime::detail::begin_report("cancellation escaped the program entry", site);
    std::fputs("  note: program exited with a failure status\n\n", stderr);
    std::fflush(stderr);
}

} // namespace carven::runtime::async
