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
    std::size_t attachments = 0;

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

enum class Progress { Runnable, Waiting, Complete };

template<typename Result, typename... Failures>
class Root;

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
    enum class Request { None, Cancelled, Stopped };
    Driver* driver;
    TaskContext* parent;
    Request request;
    auto cancel_requested() const noexcept -> bool;
    auto stop_requested() const noexcept -> bool;
    auto request_cancel() noexcept -> void;
    auto request_stop() noexcept -> void;
};

inline auto TaskContext::cancel_requested() const noexcept -> bool {
    for (auto* task = this; task != nullptr; task = task->parent) {
        if (task->request != Request::None) {
            return true;
        }
    }
    return false;
}

inline auto TaskContext::stop_requested() const noexcept -> bool {
    for (auto* task = this; task != nullptr; task = task->parent) {
        if (task->request == Request::Stopped) {
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
    friend class Root;

    auto enqueue(detail::WorkNode& node) noexcept -> void;
    auto subscribe(detail::CancellationRegistration& registration) noexcept -> void;
    auto has_ready(const TaskContext& root) const noexcept -> bool;

public:
    Driver() noexcept;
    Driver(const Driver&) = delete;
    Driver(Driver&&) = delete;
    auto operator=(const Driver&) -> Driver& = delete;
    auto operator=(Driver&&) -> Driver& = delete;
    ~Driver() noexcept;
    auto dispatch_one(TaskContext& root) noexcept -> bool;

    auto timer_queue() noexcept -> detail::TimerQueue& { return timers; }

    auto poll() noexcept -> void;
    auto attach(EventSource& source) noexcept -> void;
    auto detach(EventSource& source) noexcept -> void;
    auto wait(std::optional<std::int64_t> deadline) noexcept -> bool;
};

inline Driver::Driver() noexcept
    : task_slot(detail::current_task) {}

inline auto TaskContext::request_cancel() noexcept -> void {
    if (request == Request::None) {
        request = Request::Cancelled;
        driver->cancellation_dirty = true;
    }
}

inline auto TaskContext::request_stop() noexcept -> void {
    auto* root = this;
    while (root->parent != nullptr) {
        root = root->parent;
    }
    if (root->request != Request::Stopped) {
        root->request = Request::Stopped;
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
    detail::require_protocol(source.driver == nullptr || source.driver == this);
    if (source.attachments == 0) {
        source.driver = this;
        source.next = events;
        events = &source;
    }
    ++source.attachments;
}

inline auto Driver::detach(EventSource& source) noexcept -> void {
    detail::require_protocol(source.driver == this && source.attachments != 0);
    if (--source.attachments != 0) {
        return;
    }
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
    return timers.wait(deadline);
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

inline auto Driver::dispatch_one(TaskContext& root) noexcept -> bool {
    detail::require_protocol(!dispatching);
    auto** selected = &first;
    auto* previous = static_cast<detail::WorkNode*>(nullptr);
    while (*selected != nullptr) {
        const auto* owner = (*selected)->task;
        while (owner->parent != nullptr) {
            owner = owner->parent;
        }
        if (owner == &root) {
            break;
        }
        previous = *selected;
        selected = &previous->next;
    }
    if (*selected == nullptr) {
        return false;
    }
    const auto* node = *selected;
    *selected = node->next;
    if (last == node) {
        last = previous;
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

inline auto Driver::has_ready(const TaskContext& root) const noexcept -> bool {
    for (const auto* node = first; node != nullptr; node = node->next) {
        const auto* owner = node->task;
        while (owner->parent != nullptr) {
            owner = owner->parent;
        }
        if (owner == &root) {
            return true;
        }
    }
    return false;
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

    auto request_stop() noexcept -> void {
        detail::require_protocol(bound());
        node.task->request_stop();
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

inline auto stop_requested() noexcept -> bool {
    return detail::current_task != nullptr && detail::current_task->stop_requested();
}

inline auto request_stop() noexcept -> void {
    detail::require_protocol(detail::current_task != nullptr);
    detail::current_task->request_stop();
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
            if (result.is_stopped()) {
                state.task->request_stop();
            }
            completion.initialize(std::move(result));
        }

        auto take_completion() noexcept -> Completion<Result, Failures...> {
            detail::require_protocol(!completion.empty());
            return state.task->stop_requested()
                ? Completion<Result, Failures...>::stopped()
                : Completion<Result, Failures...>(std::move(completion));
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
    friend class Root;

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

// A host-driven root owns its coroutine frame and owns or borrows its driver.
// Shared drivers poll common event sources; advance only resumes this root's tasks,
// preserving the caller's dynamic host scope. Controls stay on the constructing
// thread; advance never waits and external providers only enqueue.
// Cancellation is cooperative. Finish or cancel and advance to completion before
// destruction so children and external registrations have closed normally.
template<typename Result, typename... Failures>
class Root final {
public:
    explicit Root(
        AsyncOperation<Result, Failures...>&& operation,
        EventSource* source = nullptr
    ) noexcept;

    Root(
        Driver& driver,
        AsyncOperation<Result, Failures...>&& operation,
        EventSource* source = nullptr
    ) noexcept;

    Root(const Root&) = delete;
    Root(Root&&) = delete;
    auto operator=(const Root&) -> Root& = delete;
    auto operator=(Root&&) -> Root& = delete;
    ~Root() noexcept;
    auto advance() noexcept -> Progress;
    auto wait(std::optional<std::int64_t> deadline = std::nullopt) noexcept -> bool;
    auto cancel() noexcept -> void;
    auto take_completion() noexcept -> Completion<Result, Failures...>;

private:
    std::optional<Driver> owned_driver;
    Driver& driver;
    TaskContext task;
    typename AsyncOperation<Result, Failures...>::Handle frame;
    detail::WorkNode startup;
    EventSource* source;
    auto start() noexcept -> void;
};

template<typename Result, typename... Failures>
Root<Result, Failures...>::Root(
    AsyncOperation<Result, Failures...>&& operation,
    EventSource* source
) noexcept
    : owned_driver(std::in_place),
      driver(*owned_driver),
      task {.driver = &driver, .parent = nullptr, .request = TaskContext::Request::None},
      frame(operation.release()),
      startup(detail::resume_node(frame, task)),
      source(source) {
    start();
}

template<typename Result, typename... Failures>
Root<Result, Failures...>::Root(
    Driver& driver,
    AsyncOperation<Result, Failures...>&& operation,
    EventSource* source
) noexcept
    : owned_driver(std::nullopt),
      driver(driver),
      task {.driver = &driver, .parent = nullptr, .request = TaskContext::Request::None},
      frame(operation.release()),
      startup(detail::resume_node(frame, task)),
      source(source) {
    start();
}

template<typename Result, typename... Failures>
auto Root<Result, Failures...>::start() noexcept -> void {
    detail::require_protocol(detail::current_task == nullptr);
    if (source != nullptr) {
        driver.attach(*source);
    }
    frame.promise().state.bind(task);
    driver.enqueue(startup);
}

template<typename Result, typename... Failures>
Root<Result, Failures...>::~Root() noexcept {
    detail::require_protocol(frame == nullptr || frame.done());
    if (frame != nullptr) {
        detail::destroy(frame, frame.promise().state);
    }
    if (source != nullptr) {
        driver.detach(*source);
    }
}

template<typename Result, typename... Failures>
auto Root<Result, Failures...>::advance() noexcept -> Progress {
    detail::require_protocol(detail::current_task == nullptr);
    if (frame == nullptr || frame.done()) {
        return Progress::Complete;
    }
    driver.poll();
    const auto progressed = driver.dispatch_one(task);
    if (frame.done()) {
        return Progress::Complete;
    }
    // A continuation may register an event source or request cancellation.
    // Poll that work before allowing the owner to wait.
    return progressed ? Progress::Runnable : Progress::Waiting;
}

template<typename Result, typename... Failures>
auto Root<Result, Failures...>::wait(std::optional<std::int64_t> deadline) noexcept -> bool {
    if (driver.has_ready(task)) {
        return true;
    }
    const auto timer = driver.timer_queue().next_deadline();
    if (timer && (!deadline || *timer < *deadline)) {
        deadline = timer;
    }
    return driver.wait(deadline);
}

template<typename Result, typename... Failures>
auto Root<Result, Failures...>::cancel() noexcept -> void {
    if (frame != nullptr && !frame.done()) {
        task.request_cancel();
    }
}

template<typename Result, typename... Failures>
auto Root<Result, Failures...>::take_completion() noexcept -> Completion<Result, Failures...> {
    detail::require_protocol(frame != nullptr && frame.done());
    auto completion = frame.promise().take_completion();
    detail::destroy(frame, frame.promise().state);
    frame = nullptr;
    if (source != nullptr) {
        driver.detach(*source);
        source = nullptr;
    }
    return completion;
}

// A host source is borrowed through root completion and child closure. All sources
// commit completions and enqueue continuations; only the driver resumes user code.
template<typename Result, typename... Failures>
inline auto drive_root(
    AsyncOperation<Result, Failures...>&& operation,
    EventSource* source
) noexcept -> Completion<Result, Failures...> {
    auto root = Root<Result, Failures...>(std::move(operation), source);
    while (true) {
        switch (root.advance()) {
            case Progress::Complete: return root.take_completion();
            case Progress::Runnable: break;
            case Progress::Waiting:  detail::require_protocol(root.wait()); break;
        }
    }
}

} // namespace carven::runtime::execution
