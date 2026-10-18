#pragma once

#include <carven/runtime/execution/completion.hpp>
#include <carven/runtime/execution/timer.hpp>

#include <coroutine>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <new>
#include <optional>
#include <utility>
#include <variant>

namespace carven::runtime::execution {

class ChildScope;
class Driver;

class EventSource {
    friend class Driver;
    EventSource* next = nullptr;
    Driver* driver = nullptr;

public:
    EventSource() noexcept = default;
    EventSource(const EventSource&) = delete;
    auto operator=(const EventSource&) -> EventSource& = delete;
    virtual ~EventSource() noexcept;

    virtual auto poll(Driver&) noexcept -> void = 0;
    virtual auto wait(std::optional<std::int64_t> deadline) noexcept -> bool = 0;
};

class OperationState;
class ResumeContinuation;
class YieldAwaiter;
struct TaskContext;

template<std::size_t Count>
class WaitReadyAwaiter;

template<typename Result, typename... Failures>
class AsyncOperation;

template<typename Result, typename... Failures>
class Child;

template<typename Result, typename... Failures>
auto drive_root(
    AsyncOperation<Result, Failures...>&& operation,
    EventSource* source = nullptr
) noexcept -> Completion<Result, Failures...>;

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

struct CompletionRegistration final {
    OperationState* operation;
    void* receiver;
    std::size_t index;
    void (*notify)(CompletionRegistration&) noexcept;
    auto detach() noexcept -> void;
};

struct CancellationRegistration final {
    CancellationRegistration* previous;
    CancellationRegistration* next;
    Driver* driver;
    TaskContext* task;
    void* receiver;
    void (*notify)(CancellationRegistration&) noexcept;
    auto detach() noexcept -> void;
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
    auto cancel_requested() const noexcept -> bool;
    auto request_cancel() noexcept -> void;
};

inline auto TaskContext::cancel_requested() const noexcept -> bool {
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
    detail::TimerQueue timers {detail::default_clock()};
    detail::CancellationRegistration* cancellation_first = nullptr;
    bool cancellation_dirty = false;
    EventSource* events = nullptr;
    friend struct TaskContext;
    friend struct detail::CancellationRegistration;
    friend class ResumeContinuation;
    friend class OperationState;
    friend class ChildScope;
    friend class YieldAwaiter;
    template<std::size_t Count>
    friend class WaitReadyAwaiter;
    template<typename T, typename... E>
    friend class Child;
    template<typename T, typename... E>
    friend auto drive_root(AsyncOperation<T, E...>&&, EventSource*) noexcept -> Completion<T, E...>;
    auto enqueue(detail::WorkNode& node) noexcept -> void;
    auto subscribe(detail::CancellationRegistration& registration) noexcept -> void;

public:
    Driver() noexcept;
    Driver(const Driver&) = delete;
    Driver(Driver&&) = delete;
    auto operator=(const Driver&) -> Driver& = delete;
    auto operator=(Driver&&) -> Driver& = delete;
    ~Driver() noexcept;
    auto dispatch_one() noexcept -> bool;

    auto timer_queue() noexcept -> detail::TimerQueue& { return timers; }

    auto poll() noexcept -> void;
    auto attach(EventSource& source) noexcept -> void;
    auto detach(EventSource& source) noexcept -> void;
    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool;
};

inline Driver::Driver() noexcept
    : task_slot(detail::current_task) {}

inline auto TaskContext::request_cancel() noexcept -> void {
    if (!requested) {
        requested = true;
        driver->cancellation_dirty = true;
    }
}

inline EventSource::~EventSource() noexcept {
    detail::require_protocol(driver == nullptr);
}

inline Driver::~Driver() noexcept {
    detail::require_protocol(
        first == nullptr && !dispatching && cancellation_first == nullptr && events == nullptr
    );
}

inline auto Driver::attach(EventSource& source) noexcept -> void {
    detail::require_protocol(source.driver == nullptr);
    source.driver = this;
    source.next = events;
    events = &source;
}

inline auto Driver::detach(EventSource& source) noexcept -> void {
    auto** current = &events;
    while (*current != &source) {
        detail::require_protocol(*current != nullptr);
        current = &(*current)->next;
    }
    *current = source.next;
    source.next = nullptr;
    source.driver = nullptr;
}

inline auto Driver::wait(std::optional<std::int64_t> deadline) noexcept -> bool {
    // Independent sources share the driver through bounded waits. A single source
    // waits directly for its events or the nearest timer deadline.
    if (events != nullptr && events->next != nullptr) {
        const auto poll_deadline = detail::steady_now() + 10000000;
        if (!deadline || poll_deadline < *deadline) {
            deadline = poll_deadline;
        }
    }
    for (auto* source = events; source != nullptr; source = source->next) {
        if (source->wait(deadline)) {
            return true;
        }
    }
    return timers.wait();
}

inline auto Driver::subscribe(detail::CancellationRegistration& registration) noexcept -> void {
    detail::require_protocol(registration.driver == nullptr);
    registration.driver = this;
    registration.previous = nullptr;
    registration.next = cancellation_first;
    if (cancellation_first != nullptr) {
        cancellation_first->previous = &registration;
    }
    cancellation_first = &registration;
}

inline auto detail::CancellationRegistration::detach() noexcept -> void {
    if (driver == nullptr) {
        return;
    }
    if (previous != nullptr) {
        previous->next = next;
    } else {
        driver->cancellation_first = next;
    }
    if (next != nullptr) {
        next->previous = previous;
    }
    driver = nullptr;
    previous = nullptr;
    next = nullptr;
}

inline auto Driver::poll() noexcept -> void {
    auto* source = events;
    while (source != nullptr) {
        auto* next = source->next;
        source->poll(*this);
        source = next;
    }
    const auto cancellation_changed = std::exchange(cancellation_dirty, false);
    if (cancellation_changed) {
        auto* registration = cancellation_first;
        while (registration != nullptr) {
            auto* next = registration->next;
            if (registration->task->cancel_requested()) {
                registration->notify(*registration);
            }
            registration = next;
        }
    }
    timers.poll(cancellation_changed);
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
    const auto* node = first;
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

    auto cancel_requested() const noexcept -> bool {
        detail::require_protocol(bound());
        return node.task->cancel_requested();
    }

    auto driver() const noexcept -> Driver& {
        detail::require_protocol(bound());
        return *node.task->driver;
    }

    auto enqueue(Driver& driver) noexcept -> void {
        detail::require_protocol(bound() && node.task->driver == &driver);
        driver.enqueue(node);
    }
};

class OperationState final {
public:
    TaskContext* task = nullptr;
    std::variant<
        std::monostate,
        std::coroutine_handle<>,
        detail::WorkNode*,
        detail::CompletionRegistration*>
        waiter;

    auto bind(TaskContext& context) noexcept -> void;
    auto publish() noexcept -> std::coroutine_handle<>;
    auto wait(std::coroutine_handle<> continuation) noexcept -> void;
    auto wait(detail::WorkNode& continuation) noexcept -> void;
    auto wait(detail::CompletionRegistration& registration) noexcept -> void;
};

inline auto detail::CompletionRegistration::detach() noexcept -> void {
    if (operation == nullptr) {
        return;
    }
    const auto* selected = std::get_if<CompletionRegistration*>(&operation->waiter);
    require_protocol(selected != nullptr && *selected == this);
    operation->waiter.emplace<std::monostate>();
    operation = nullptr;
}

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
    if (const auto* selected = std::get_if<detail::CompletionRegistration*>(&waiter)) {
        auto* registration = *selected;
        waiter.emplace<std::monostate>();
        registration->operation = nullptr;
        registration->notify(*registration);
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

inline auto OperationState::wait(detail::CompletionRegistration& registration) noexcept -> void {
    detail::require_protocol(
        std::holds_alternative<std::monostate>(waiter) && registration.operation == nullptr
    );
    registration.operation = this;
    waiter = &registration;
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

inline auto cancel_requested() noexcept -> bool {
    return detail::current_task != nullptr && detail::current_task->cancel_requested();
}

template<typename Result, typename... Failures>
class AsyncOperation final {
public:
    class promise_type final {
        // C++20 supplies only the frame size. Current Carven value carriers need
        // at most 32-byte alignment, including AVX2 vectors retained by a frame.
        static constexpr auto allocation_alignment =
            std::align_val_t {alignof(std::max_align_t) > 32 ? alignof(std::max_align_t) : 32};

    public:
        OperationState state;
        Completion<Result, Failures...> completion;

        promise_type() noexcept = default;

        static auto operator new(std::size_t size) noexcept -> void* {
            auto* storage = ::operator new(size, allocation_alignment, std::nothrow);
            if (storage == nullptr) {
                std::terminate();
            }
            return storage;
        }

        static auto operator delete(void* storage, std::size_t) noexcept -> void {
            ::operator delete(storage, allocation_alignment);
        }

        auto get_return_object() noexcept -> AsyncOperation {
            return AsyncOperation(std::coroutine_handle<promise_type>::from_promise(*this));
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

    explicit AsyncOperation(Handle handle) noexcept
        : frame(handle) {}
    template<typename T, typename... E>
    friend class Child;
    template<typename T, typename... E>
    friend auto drive_root(AsyncOperation<T, E...>&&, EventSource*) noexcept -> Completion<T, E...>;

    auto release() noexcept -> Handle {
        detail::require_protocol(frame != nullptr);
        return std::exchange(frame, nullptr);
    }

public:
    AsyncOperation(const AsyncOperation&) = delete;

    AsyncOperation(AsyncOperation&& other) noexcept
        : frame(std::exchange(other.frame, nullptr)) {}

    auto operator=(const AsyncOperation&) -> AsyncOperation& = delete;

    auto operator=(AsyncOperation&& other) noexcept -> AsyncOperation& {
        if (this != &other) {
            if (frame != nullptr) {
                detail::destroy(frame, frame.promise().state);
            }
            frame = std::exchange(other.frame, nullptr);
        }
        return *this;
    }

    ~AsyncOperation() noexcept {
        if (frame != nullptr) {
            detail::destroy(frame, frame.promise().state);
        }
    }

    // The AsyncOperation outlives suspension and owns the borrowed frame.
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
            const auto& caller = handle.promise().state;
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

class CancelPointAwaiter final {
public:
    auto await_ready() const noexcept -> bool { return true; }

    auto await_suspend(std::coroutine_handle<>) const noexcept -> void {}

    auto await_resume() const noexcept -> Completion<void> {
        return cancel_requested() ? Completion<void>::cancelled() : Completion<void>::success();
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
        const auto& state = handle.promise().state;
        resume = detail::resume_node(handle, *state.task);
        state.task->driver->enqueue(resume);
    }

    auto await_resume() const noexcept -> Completion<void> { return Completion<void>::success(); }
};

inline auto await_cancel_point() noexcept -> CancelPointAwaiter {
    return {};
}

inline auto await_yield_once() noexcept -> YieldAwaiter {
    return {};
}

inline auto cancel_point() noexcept -> AsyncOperation<void> {
    co_return co_await await_cancel_point();
}

inline auto yield_once() noexcept -> AsyncOperation<void> {
    co_return co_await await_yield_once();
}

// A host source is borrowed through root completion and child closure. All sources
// commit completions and enqueue continuations; only the driver resumes user code.
template<typename Result, typename... Failures>
inline auto drive_root(
    AsyncOperation<Result, Failures...>&& operation,
    EventSource* source
) noexcept -> Completion<Result, Failures...> {
    detail::require_protocol(detail::current_task == nullptr);
    auto driver = Driver();
    if (source != nullptr) {
        driver.attach(*source);
    }
    auto task = TaskContext {.driver = &driver, .parent = nullptr, .requested = false};
    const auto frame = operation.release();
    auto& state = frame.promise().state;
    state.bind(task);
    auto startup = detail::resume_node(frame, task);
    driver.enqueue(startup);
    while (!frame.done()) {
        driver.poll();
        const auto progressed = driver.dispatch_one();
        if (!progressed) {
            detail::require_protocol(driver.wait(driver.timer_queue().next_deadline()));
        }
    }
    auto completion = frame.promise().take_completion();
    detail::destroy(frame, state);
    if (source != nullptr) {
        driver.detach(*source);
    }
    return completion;
}

} // namespace carven::runtime::execution
