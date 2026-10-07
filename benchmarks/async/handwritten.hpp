#pragma once

#include <coroutine>
#include <cstdint>
#include <exception>
#include <utility>

namespace handwritten {

// Minimal lazy value coroutine: inline payload and symmetric transfer.
// This baseline has no typed failure, cancellation, or automatic child closure.
class Task final {
public:
    class promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    class promise_type final {
    public:
        std::uint64_t result;
        std::coroutine_handle<> continuation;
        promise_type() noexcept;
        auto get_return_object() noexcept -> Task;
        auto initial_suspend() noexcept -> std::suspend_always;
        auto unhandled_exception() noexcept -> void;
        auto return_value(std::uint64_t value) noexcept -> void;

        struct FinalAwaiter final {
            auto await_ready() const noexcept -> bool;
            auto await_suspend(Handle handle) const noexcept -> std::coroutine_handle<>;
            auto await_resume() const noexcept -> void;
        };

        auto final_suspend() noexcept -> FinalAwaiter;
    };

    explicit Task(Handle handle) noexcept;
    Task(const Task&) = delete;
    Task(Task&& other) noexcept;
    auto operator=(const Task&) -> Task& = delete;
    auto operator=(Task&&) -> Task& = delete;
    ~Task() noexcept;

    struct Awaiter final {
        Handle handle;
        auto await_ready() const noexcept -> bool;
        auto await_suspend(std::coroutine_handle<> parent) const noexcept
            -> std::coroutine_handle<>;
        auto await_resume() const noexcept -> std::uint64_t;
    };

    auto operator co_await() const noexcept -> Awaiter;
    auto start() noexcept -> void;
    auto ready() const noexcept -> bool;

private:
    Handle handle;
};

inline Task::promise_type::promise_type() noexcept
    : result(0),
      continuation(std::noop_coroutine()) {}

inline auto Task::promise_type::get_return_object() noexcept -> Task {
    return Task(Handle::from_promise(*this));
}

inline auto Task::promise_type::initial_suspend() noexcept -> std::suspend_always {
    return {};
}

inline auto Task::promise_type::unhandled_exception() noexcept -> void {
    std::terminate();
}

inline auto Task::promise_type::return_value(std::uint64_t value) noexcept -> void {
    result = value;
}

inline auto Task::promise_type::FinalAwaiter::await_ready() const noexcept -> bool {
    return false;
}

inline auto Task::promise_type::FinalAwaiter::await_suspend(Handle handle) const noexcept
    -> std::coroutine_handle<> {
    return handle.promise().continuation;
}

inline auto Task::promise_type::FinalAwaiter::await_resume() const noexcept -> void {}

inline auto Task::promise_type::final_suspend() noexcept -> FinalAwaiter {
    return {};
}

inline Task::Task(Handle handle) noexcept
    : handle(handle) {}

inline Task::Task(Task&& other) noexcept
    : handle(std::exchange(other.handle, {})) {}

inline Task::~Task() noexcept {
    if (handle) {
        handle.destroy();
    }
}

inline auto Task::Awaiter::await_ready() const noexcept -> bool {
    return handle.done();
}

inline auto Task::Awaiter::await_suspend(std::coroutine_handle<> parent) const noexcept
    -> std::coroutine_handle<> {
    handle.promise().continuation = parent;
    return handle;
}

inline auto Task::Awaiter::await_resume() const noexcept -> std::uint64_t {
    return handle.promise().result;
}

inline auto Task::operator co_await() const noexcept -> Awaiter {
    return Awaiter {.handle = handle};
}

inline auto Task::start() noexcept -> void {
    handle.resume();
}

inline auto Task::ready() const noexcept -> bool {
    return handle.done();
}

} // namespace handwritten
