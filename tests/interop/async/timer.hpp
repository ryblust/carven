#pragma once

#include <carven/runtime/async/async.hpp>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <optional>
#include <thread>

namespace async_interop {

// A single-thread provider fixture. An unlinked timer has no external callback.
class TimerQueue final {
public:
    using Clock = std::chrono::steady_clock;

    class Awaiter final {
        friend class TimerQueue;
        enum class Outcome { Pending, Success, Cancelled };

        TimerQueue& queue;
        Clock::duration delay;
        Clock::time_point deadline {};
        Awaiter* next = nullptr;
        Outcome outcome = Outcome::Pending;
        carven::runtime::async::ResumeContinuation resume;

    public:
        Awaiter(TimerQueue& provider, Clock::duration duration) noexcept
            : queue(provider),
              delay(duration) {}

        Awaiter(const Awaiter&) = delete;
        Awaiter(Awaiter&&) = delete;
        auto operator=(const Awaiter&) -> Awaiter& = delete;
        auto operator=(Awaiter&&) -> Awaiter& = delete;

        ~Awaiter() noexcept { assert(!resume.bound() || outcome != Outcome::Pending); }

        auto await_ready() const noexcept -> bool { return false; }

        template<typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
            resume.bind(handle);
            if (resume.cancellation_requested()) {
                outcome = Outcome::Cancelled;
                return false;
            }
            if (delay <= Clock::duration::zero()) {
                outcome = Outcome::Success;
                return false;
            }
            deadline = Clock::now() + delay;
            queue.register_timer(*this);
            return true;
        }

        auto await_resume() const noexcept -> carven::runtime::async::Completion<void> {
            assert(outcome != Outcome::Pending);
            return outcome == Outcome::Cancelled
                ? carven::runtime::async::Completion<void>::cancelled()
                : carven::runtime::async::Completion<void>::success();
        }
    };

private:
    Awaiter* first = nullptr;
    Awaiter* last = nullptr;
    std::size_t registrations = 0;
    std::size_t completions = 0;
    std::size_t waits = 0;

    auto register_timer(Awaiter& timer) noexcept -> void {
        if (last != nullptr) {
            last->next = &timer;
        } else {
            first = &timer;
        }
        last = &timer;
        ++registrations;
    }

public:
    TimerQueue() noexcept = default;
    TimerQueue(const TimerQueue&) = delete;
    TimerQueue(TimerQueue&&) = delete;
    auto operator=(const TimerQueue&) -> TimerQueue& = delete;
    auto operator=(TimerQueue&&) -> TimerQueue& = delete;

    ~TimerQueue() noexcept { assert(first == nullptr); }

    // A positive delay must produce a representable steady-clock deadline.
    // An existing cancel request wins; otherwise a nonpositive delay succeeds immediately.
    auto wait_for(Clock::duration delay) noexcept -> Awaiter { return Awaiter(*this, delay); }

    // Commit each outcome before enqueueing; callbacks never resume a frame here.
    auto poll(carven::runtime::async::Driver& driver) noexcept -> void {
        const auto now = Clock::now();
        auto** link = &first;
        auto* previous = static_cast<Awaiter*>(nullptr);
        while (*link != nullptr) {
            auto* timer = *link;
            const auto cancelled = timer->resume.cancellation_requested();
            if (!cancelled && timer->deadline > now) {
                previous = timer;
                link = &timer->next;
                continue;
            }
            *link = timer->next;
            if (last == timer) {
                last = previous;
            }
            timer->next = nullptr;
            timer->outcome = cancelled ? Awaiter::Outcome::Cancelled : Awaiter::Outcome::Success;
            ++completions;
            timer->resume.enqueue(driver);
        }
    }

    auto next_deadline() const noexcept -> std::optional<Clock::time_point> {
        auto result = std::optional<Clock::time_point>();
        for (auto* timer = first; timer != nullptr; timer = timer->next) {
            if (!result || timer->deadline < *result) {
                result = timer->deadline;
            }
        }
        return result;
    }

    auto wait() noexcept -> bool {
        const auto deadline = next_deadline();
        if (!deadline) {
            return false;
        }
        ++waits;
        std::this_thread::sleep_until(*deadline);
        return true;
    }

    auto pending_count() const noexcept -> std::size_t {
        auto count = std::size_t(0);
        for (auto* timer = first; timer != nullptr; timer = timer->next) {
            ++count;
        }
        return count;
    }

    auto registration_count() const noexcept -> std::size_t { return registrations; }

    auto completion_count() const noexcept -> std::size_t { return completions; }

    auto wait_count() const noexcept -> std::size_t { return waits; }
};

} // namespace async_interop
