#pragma once

#include <carven/runtime/execution/execution.hpp>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <optional>
#include <thread>

namespace async_interop {

// A single-thread provider fixture. An unlinked timer has no external callback.
class TimerQueue final : public carven::runtime::execution::EventSource {
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
        carven::runtime::execution::ResumeContinuation resume;

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
            if (resume.cancel_requested()) {
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

        auto await_resume() const noexcept -> carven::runtime::execution::Completion<void> {
            assert(outcome != Outcome::Pending);
            return outcome == Outcome::Cancelled
                ? carven::runtime::execution::Completion<void>::cancelled()
                : carven::runtime::execution::Completion<void>::success();
        }
    };

private:
    Awaiter* first = nullptr;
    Awaiter* last = nullptr;

    auto register_timer(Awaiter& timer) noexcept -> void {
        if (last != nullptr) {
            last->next = &timer;
        } else {
            first = &timer;
        }
        last = &timer;
    }

public:
    TimerQueue() noexcept = default;
    TimerQueue(const TimerQueue&) = delete;
    TimerQueue(TimerQueue&&) = delete;
    auto operator=(const TimerQueue&) -> TimerQueue& = delete;
    auto operator=(TimerQueue&&) -> TimerQueue& = delete;

    ~TimerQueue() noexcept override { assert(first == nullptr); }

    // A positive delay must produce a representable steady-clock deadline.
    // An existing cancel request wins; otherwise a nonpositive delay succeeds immediately.
    auto wait_for(Clock::duration delay) noexcept -> Awaiter { return Awaiter(*this, delay); }

    // Commit each outcome before enqueueing; callbacks never resume a frame here.
    auto poll(carven::runtime::execution::Driver& driver) noexcept -> void override {
        const auto now = Clock::now();
        auto** link = &first;
        auto* previous = static_cast<Awaiter*>(nullptr);
        while (*link != nullptr) {
            auto* timer = *link;
            const auto cancelled = timer->resume.cancel_requested();
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

    auto wait(std::optional<std::int64_t> limit) noexcept -> bool override {
        auto deadline = next_deadline();
        if (!deadline) {
            return false;
        }
        if (limit) {
            const auto external = Clock::time_point(
                std::chrono::ceil<Clock::duration>(std::chrono::nanoseconds(*limit))
            );
            if (external < *deadline) {
                deadline = external;
            }
        }
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
};

} // namespace async_interop
