#pragma once

#include <cstdint>

namespace carven::runtime::execution {

namespace detail {

class TimerAwaiter final {
    ResumeContinuation resume;
    Driver* driver = nullptr;
    TimerRegistration registration;
    bool cancelled = false;

public:
    explicit TimerAwaiter(std::int64_t deadline) noexcept
        : registration {
              .queue = nullptr,
              .previous = nullptr,
              .next = nullptr,
              .deadline = deadline,
              .receiver = this,
              .cancellation_requested =
                  [](void* receiver) noexcept {
                      return static_cast<TimerAwaiter*>(receiver)->resume.cancellation_requested();
                  },
              .complete =
                  [](void* receiver, bool cancelled) noexcept {
                      auto& timer = *static_cast<TimerAwaiter*>(receiver);
                      timer.cancelled = cancelled;
                      timer.resume.enqueue(*timer.driver);
                  }
          } {}

    TimerAwaiter(const TimerAwaiter&) = delete;
    TimerAwaiter(TimerAwaiter&&) = delete;
    auto operator=(const TimerAwaiter&) -> TimerAwaiter& = delete;
    auto operator=(TimerAwaiter&&) -> TimerAwaiter& = delete;

    ~TimerAwaiter() noexcept { require_protocol(registration.queue == nullptr); }

    auto await_ready() const noexcept -> bool { return false; }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
        resume.bind(handle);
        driver = handle.promise().state.task->driver;
        auto& queue = driver->timer_queue();
        cancelled = resume.cancellation_requested();
        if (cancelled || registration.deadline <= queue.now()) {
            return false;
        }
        queue.insert(registration);
        return true;
    }

    auto await_resume() const noexcept -> Completion<void> {
        return cancelled ? Completion<void>::cancelled() : Completion<void>::success();
    }
};

} // namespace detail

inline auto now() noexcept -> std::int64_t {
    return detail::steady_now();
}

inline auto sleep_until(std::int64_t deadline) noexcept -> AsyncOperation<void> {
    co_return co_await detail::TimerAwaiter(deadline);
}

} // namespace carven::runtime::execution
