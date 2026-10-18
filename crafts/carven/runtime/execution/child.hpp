#pragma once

#include <carven/runtime/execution/execution.hpp>
#include <carven/runtime/value/tuple.hpp>

#include <array>
#include <coroutine>
#include <cstddef>
#include <exception>
#include <utility>
#include <variant>

namespace carven::runtime::execution {

enum class ClosingPolicy { CloseOnly, RequestCancelAndClose };

template<typename... Children>
auto wait_ready(Children&... children) noexcept -> WaitReadyAwaiter<sizeof...(Children)>;

namespace detail {

template<typename... Inputs>
struct CompositionSlots;

struct ChildSlot final {
    ChildSlot* next;
    ChildScope* scope;
    std::coroutine_handle<> frame;
    OperationState* operation;
    void* child;
    void (*discard)(void*) noexcept;
    bool (*cancelled)(void*) noexcept;
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
    template<std::size_t Count>
    friend class WaitReadyAwaiter;

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
    auto start(AsyncOperation<T, E...>&& operation) noexcept -> Child<T, E...> {
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
        for (const auto* child = scope->children; child != nullptr; child = child->next) {
            if (child->frame != nullptr) {
                child->operation->task->request_cancel();
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
    using Handle = typename AsyncOperation<Result, Failures...>::Handle;

    detail::ChildSlot slot;
    TaskContext task;
    detail::WorkNode startup;

    friend class ChildScope;

    template<typename... Inputs>
    friend struct detail::CompositionSlots;

    template<typename T, typename... E>
    friend auto cancel(Child<T, E...>&) noexcept -> void;

    template<typename... Children>
    friend auto wait_ready(Children&...) noexcept -> WaitReadyAwaiter<sizeof...(Children)>;

    Child(ChildScope& scope, AsyncOperation<Result, Failures...>&& operation) noexcept
        : slot {
              .next = nullptr,
              .scope = nullptr,
              .frame = operation.release(),
              .operation = nullptr,
              .child = this,
              .discard = [](void* child) noexcept { static_cast<Child*>(child)->discard(); },
              .cancelled =
                  [](void* child) noexcept {
                      return static_cast<Child*>(child)
                          ->frame_handle()
                          .promise()
                          .completion.is_cancelled();
                  }
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

    class CloseAwaiter final {
        Child* child;
        detail::WorkNode
            wake {.next = nullptr, .task = nullptr, .argument = nullptr, .invoke = nullptr};

    public:
        explicit CloseAwaiter(Child& value) noexcept
            : child(&value) {}

        CloseAwaiter(const CloseAwaiter&) = delete;
        CloseAwaiter(CloseAwaiter&&) = delete;
        auto operator=(const CloseAwaiter&) -> CloseAwaiter& = delete;
        auto operator=(CloseAwaiter&&) -> CloseAwaiter& = delete;

        auto await_ready() const noexcept -> bool { return false; }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
            auto& caller = handle.promise().state;
            detail::require_protocol(
                child->slot.scope == nullptr || child->slot.scope->owner == &caller
            );
            if (child->slot.frame == nullptr) {
                return false;
            }
            child->task.request_cancel();
            if (child->slot.frame.done()) {
                return false;
            }
            wake = detail::resume_node(handle, *caller.task);
            child->slot.operation->wait(wake);
            return true;
        }

        auto await_resume() noexcept -> void {
            if (child->slot.frame != nullptr) {
                detail::require_protocol(child->slot.frame.done());
                child->discard();
            }
        }
    };

    auto close() & noexcept -> CloseAwaiter { return CloseAwaiter(*this); }
};

template<typename Result, typename... Failures>
inline auto close(Child<Result, Failures...>& child) noexcept ->
    typename Child<Result, Failures...>::CloseAwaiter {
    return child.close();
}

// Registrations borrow child state until one terminal publication commits the
// selection. All registrations are detached before the sole parent wake is queued.
template<std::size_t Count>
class WaitReadyAwaiter final {
    static_assert(Count != 0);
    std::array<detail::ChildSlot*, Count> children;
    std::array<detail::CompletionRegistration, Count> registrations {};
    detail::WorkNode
        wake {.next = nullptr, .task = nullptr, .argument = nullptr, .invoke = nullptr};
    std::size_t winner = Count;
    bool cancelled = false;
    bool committed = false;
    const std::array<bool, Count>* active;
    bool accept_cancellation;
    detail::CancellationRegistration cancellation_registration {
        .previous = nullptr,
        .next = nullptr,
        .driver = nullptr,
        .task = nullptr,
        .receiver = this,
        .notify = [](detail::CancellationRegistration& registration) noexcept {
            auto& selection = *static_cast<WaitReadyAwaiter*>(registration.receiver);
            selection.commit_cancellation();
            selection.wake.task->driver->enqueue(selection.wake);
        }
    };

    auto detach() noexcept -> void {
        for (auto& registration : registrations) {
            registration.detach();
        }
        cancellation_registration.detach();
    }

    auto commit_cancellation() noexcept -> void {
        detail::require_protocol(!committed);
        committed = true;
        cancelled = true;
        detach();
    }

    auto commit(std::size_t index) noexcept -> void {
        detail::require_protocol(!committed && children[index]->frame.done());
        committed = true;
        winner = index;
        cancelled = children[index]->cancelled(children[index]->child);
        detach();
    }

public:
    explicit WaitReadyAwaiter(
        std::array<detail::ChildSlot*, Count> values,
        const std::array<bool, Count>* active,
        bool accept_cancellation
    ) noexcept
        : children(values),
          active(active),
          accept_cancellation(accept_cancellation) {
        for (std::size_t index = 0; index < Count; ++index) {
            registrations[index] = {
                .operation = nullptr,
                .receiver = this,
                .index = index,
                .notify = [](detail::CompletionRegistration& registration) noexcept {
                    auto& selection = *static_cast<WaitReadyAwaiter*>(registration.receiver);
                    selection.commit(registration.index);
                    selection.wake.task->driver->enqueue(selection.wake);
                }
            };
        }
    }

    WaitReadyAwaiter(const WaitReadyAwaiter&) = delete;
    WaitReadyAwaiter(WaitReadyAwaiter&&) = delete;
    auto operator=(const WaitReadyAwaiter&) -> WaitReadyAwaiter& = delete;
    auto operator=(WaitReadyAwaiter&&) -> WaitReadyAwaiter& = delete;

    ~WaitReadyAwaiter() noexcept {
        detail::require_protocol(cancellation_registration.driver == nullptr);
        for (const auto& registration : registrations) {
            detail::require_protocol(registration.operation == nullptr);
        }
    }

    auto await_ready() const noexcept -> bool { return false; }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
        auto& caller = handle.promise().state;
        auto ready = Count;
        for (std::size_t index = 0; index < Count; ++index) {
            if (active != nullptr && !(*active)[index]) {
                continue;
            }
            const auto* child = children[index];
            detail::require_protocol(
                child->frame != nullptr
                && child->scope != nullptr
                && child->scope->owner == &caller
                && std::holds_alternative<std::monostate>(child->operation->waiter)
            );
            children[index]->operation->wait(registrations[index]);
            if (ready == Count && child->frame.done()) {
                ready = index;
            }
        }
        if (accept_cancellation && caller.task->cancel_requested()) {
            commit_cancellation();
            return false;
        }
        if (ready != Count) {
            commit(ready);
            return false;
        }
        wake = detail::resume_node(handle, *caller.task);
        if (accept_cancellation) {
            cancellation_registration.task = caller.task;
            caller.task->driver->subscribe(cancellation_registration);
        }
        return true;
    }

    auto await_resume() const noexcept -> carven::runtime::Tuple<std::size_t, bool> {
        detail::require_protocol(committed);
        return {.head = winner, .tail = {.head = cancelled, .tail = {}}};
    }
};

template<typename... Children>
inline auto wait_ready(Children&... children) noexcept -> WaitReadyAwaiter<sizeof...(Children)> {
    return WaitReadyAwaiter<sizeof...(Children)>({(&children.slot)...}, nullptr, false);
}

template<typename Result, typename... Failures>
inline auto cancel(Child<Result, Failures...>& child) noexcept -> void {
    detail::require_protocol(child.slot.frame != nullptr && child.slot.scope != nullptr);
    // Recording intent is immediate; descendants inspect ancestry iteratively.
    child.task.request_cancel();
}

} // namespace carven::runtime::execution
