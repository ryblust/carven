#pragma once

#include <carven/runtime/execution/clock.hpp>

#include <cstdint>
#include <exception>
#include <optional>

namespace carven::runtime::execution::detail {

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
    explicit TimerQueue(Clock selected) noexcept
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

    auto wait(std::optional<std::int64_t> deadline = std::nullopt) noexcept -> bool {
        if (first == nullptr) {
            return false;
        }
        const auto selected = deadline && *deadline < first->deadline ? *deadline : first->deadline;
        clock.wait_until(clock.state, selected);
        return true;
    }
};

} // namespace carven::runtime::execution::detail
