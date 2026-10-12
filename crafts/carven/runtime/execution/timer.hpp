#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <ratio>
#include <thread>
#include <type_traits>

namespace carven::runtime::execution::detail {

using SteadyDuration = std::chrono::steady_clock::duration;
using SteadyRep = SteadyDuration::rep;
using NanosecondsPerTick = std::ratio_divide<SteadyDuration::period, std::nano>;

static_assert(
    std::is_integral_v<SteadyRep>
        && std::is_signed_v<SteadyRep>
        && std::numeric_limits<SteadyRep>::digits == 63,
    "execution timers require a signed 64-bit integral steady-clock representation"
);

static_assert(
    NanosecondsPerTick::den == 1 && NanosecondsPerTick::num > 0,
    "execution timers require steady-clock ticks containing whole nanoseconds"
);

inline auto steady_now() noexcept -> std::int64_t {
    constexpr auto factor = NanosecondsPerTick::num;
    const auto ticks =
        static_cast<std::int64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    if (ticks < std::numeric_limits<std::int64_t>::lowest() / factor
        || ticks > std::numeric_limits<std::int64_t>::max() / factor) {
        std::terminate();
    }
    return ticks * factor;
}

inline auto steady_wait_until(std::int64_t deadline) noexcept -> void {
    constexpr auto factor = NanosecondsPerTick::num;
    auto ticks = deadline / factor;
    // Round toward the later clock tick, including negative epoch offsets.
    if (deadline % factor > 0) {
        ++ticks;
    }
    std::this_thread::sleep_until(
        std::chrono::steady_clock::time_point(SteadyDuration(static_cast<SteadyRep>(ticks)))
    );
}

struct Clock final {
    void* state;
    std::int64_t (*now)(void*) noexcept;
    void (*wait_until)(void*, std::int64_t) noexcept;
};

inline auto default_clock() noexcept -> Clock {
    return Clock {
        .state = nullptr,
        .now = [](void*) noexcept { return steady_now(); },
        .wait_until = [](void*, std::int64_t deadline) noexcept { steady_wait_until(deadline); }
    };
}

class TimerQueue;

struct TimerRegistration final {
    TimerQueue* queue;
    TimerRegistration* previous;
    TimerRegistration* next;
    std::int64_t deadline;
    void* receiver;
    bool (*cancel_requested)(void*) noexcept;
    void (*complete)(void*, bool) noexcept;
};

// Awaiter-owned registrations are ordered by deadline; equal deadlines keep
// registration order. A committed timer is unlinked before its notification.
// Completion callbacks may enqueue work; they must not resume user code or
// reenter this queue. Insertion is O(N); due polling visits only expired heads.
class TimerQueue final {
    TimerRegistration* first = nullptr;
    Clock clock;

    auto finish(TimerRegistration& registration, bool cancelled) noexcept -> void {
        if (registration.previous != nullptr) {
            registration.previous->next = registration.next;
        } else {
            first = registration.next;
        }
        if (registration.next != nullptr) {
            registration.next->previous = registration.previous;
        }
        registration.queue = nullptr;
        registration.previous = nullptr;
        registration.next = nullptr;
        registration.complete(registration.receiver, cancelled);
    }

public:
    explicit TimerQueue(Clock selected = default_clock()) noexcept
        : clock(selected) {}

    TimerQueue(const TimerQueue&) = delete;
    TimerQueue(TimerQueue&&) = delete;
    auto operator=(const TimerQueue&) -> TimerQueue& = delete;
    auto operator=(TimerQueue&&) -> TimerQueue& = delete;

    ~TimerQueue() noexcept {
        if (first != nullptr) {
            std::terminate();
        }
    }

    auto now() const noexcept -> std::int64_t { return clock.now(clock.state); }

    auto insert(TimerRegistration& registration) noexcept -> void {
        if (registration.queue != nullptr) {
            std::terminate();
        }
        auto* previous = static_cast<TimerRegistration*>(nullptr);
        auto* next = first;
        while (next != nullptr && next->deadline <= registration.deadline) {
            previous = next;
            next = next->next;
        }
        registration.queue = this;
        registration.previous = previous;
        registration.next = next;
        if (previous != nullptr) {
            previous->next = &registration;
        } else {
            first = &registration;
        }
        if (next != nullptr) {
            next->previous = &registration;
        }
    }

    // The driver supplies cancellation changes. Check affected tasks before due
    // deadlines so a request and expiry in the same poll select cancellation.
    auto poll(bool cancellation_changed) noexcept -> void {
        if (cancellation_changed) {
            auto* registration = first;
            while (registration != nullptr) {
                auto* next = registration->next;
                if (registration->cancel_requested(registration->receiver)) {
                    finish(*registration, true);
                }
                registration = next;
            }
        }
        if (first == nullptr) {
            return;
        }
        const auto instant = now();
        while (first != nullptr && first->deadline <= instant) {
            auto& registration = *first;
            finish(registration, false);
        }
    }

    auto next_deadline() const noexcept -> std::optional<std::int64_t> {
        return first != nullptr ? std::optional(first->deadline) : std::nullopt;
    }

    auto wait() noexcept -> bool {
        if (first == nullptr) {
            return false;
        }
        clock.wait_until(clock.state, first->deadline);
        return true;
    }
};

} // namespace carven::runtime::execution::detail
