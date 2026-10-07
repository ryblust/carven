#include "timer.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace {
namespace async = carven::runtime::async;
using async_interop::TimerQueue;
using namespace std::chrono_literals;

auto check(bool condition, const char* message) noexcept -> void {
    if (!condition) {
        std::fprintf(stderr, "timer contract failed: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

auto balanced(const TimerQueue& timers) noexcept -> void {
    check(timers.pending_count() == 0, "no registration remains after completion");
    check(
        timers.registration_count() == timers.completion_count(),
        "registration/unregistration balance"
    );
}

struct Observation final {
    int started;
    int finished;
    bool cancelled;
};

class Lifetime final {
    int& live;
    int& destroyed;

public:
    Lifetime(int& count, int& destructions) noexcept
        : live(count),
          destroyed(destructions) {
        ++live;
    }

    ~Lifetime() noexcept {
        --live;
        ++destroyed;
    }
};

auto timer(TimerQueue& timers, TimerQueue::Clock::duration delay, Observation& observed) noexcept
    -> async::Operation<void> {
    ++observed.started;
    auto result = co_await timers.wait_for(delay);
    observed.cancelled = result.is_cancelled();
    ++observed.finished;
    co_return result;
}

auto captured_timer(
    TimerQueue& timers,
    std::unique_ptr<Lifetime> capture,
    Observation& observed
) noexcept -> async::Operation<void> {
    check(capture != nullptr, "cold capture remains owned when body starts");
    co_return co_await timer(timers, 1h, observed);
}

auto cold() noexcept -> void {
    auto timers = TimerQueue();
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto live = 0;
    auto destroyed = 0;
    {
        auto operation =
            captured_timer(timers, std::make_unique<Lifetime>(live, destroyed), observed);
        check(live == 1 && destroyed == 0, "unstarted operation owns capture");
        check(
            observed.started == 0 && timers.registration_count() == 0,
            "construction does not start timer"
        );
    }
    check(
        live == 0 && destroyed == 1 && observed.started == 0,
        "unstarted capture destroyed exactly once"
    );
    balanced(timers);
}

auto immediate() noexcept -> void {
    for (const auto delay : {0ms, -1ms}) {
        auto timers = TimerQueue();
        auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
        auto result = async::drive_root(timer(timers, delay, observed), &timers);
        check(
            result.success_if() != nullptr && observed.started == 1 && observed.finished == 1,
            "nonpositive timer completes successfully once"
        );
        check(
            timers.registration_count() == 0 && timers.wait_count() == 0,
            "nonpositive timer never registers or waits"
        );
        balanced(timers);
    }
}

// Delay the first provider poll until the driver is idle. This establishes a
// real idle wait even if the process was descheduled beyond the deadline.
class IdleWait final {
    TimerQueue& timers;
    bool waited = false;
    std::optional<TimerQueue::Clock::time_point> deadline;

public:
    explicit IdleWait(TimerQueue& provider) noexcept
        : timers(provider) {}

    auto poll(async::Driver& driver) noexcept -> void {
        if (waited || timers.pending_count() == 0) {
            timers.poll(driver);
        }
    }

    auto wait() noexcept -> bool {
        check(timers.pending_count() == 1, "future timer suspends before idle wait");
        deadline = timers.next_deadline();
        check(deadline.has_value(), "registered timer exposes its actual deadline");
        waited = true;
        return timers.wait();
    }

    auto completed_after_deadline() const noexcept -> bool {
        return deadline && TimerQueue::Clock::now() >= *deadline;
    }
};

auto future() noexcept -> void {
    auto timers = TimerQueue();
    auto pump = IdleWait(timers);
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result = async::drive_root(timer(timers, 2ms, observed), &pump);
    check(
        result.success_if() != nullptr && observed.finished == 1,
        "future timer delivers success once"
    );
    check(pump.completed_after_deadline(), "future timer never completes before deadline");
    check(
        timers.registration_count() == 1 && timers.wait_count() >= 1,
        "future timer registers and uses idle wait"
    );
    balanced(timers);
}

// Hold provider delivery while a parent establishes the registered boundary.
// Timers keep real deadlines; only delivery policy is gated on the same thread.
class PollGate final {
    TimerQueue& timers;
    bool& released;

public:
    PollGate(TimerQueue& provider, bool& allow_poll) noexcept
        : timers(provider),
          released(allow_poll) {}

    auto poll(async::Driver& driver) noexcept -> void {
        if (released) {
            timers.poll(driver);
        }
    }

    auto wait() noexcept -> bool { return timers.wait(); }
};

auto sibling_timer(
    TimerQueue& timers,
    int number,
    std::vector<int>& trace,
    int& live,
    int& destroyed
) noexcept -> async::Operation<void> {
    auto lifetime = Lifetime(live, destroyed);
    trace.push_back(number);
    auto result = co_await timers.wait_for(2ms);
    check(result.success_if() != nullptr, "ordinary child close waits for timer success");
    trace.push_back(number + 10);
    co_return result;
}

auto siblings_body(
    TimerQueue& timers,
    std::vector<int>& trace,
    int& live,
    int& destroyed,
    bool& released
) noexcept -> async::Operation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto first = scope.start(sibling_timer(timers, 1, trace, live, destroyed));
    auto second = scope.start(sibling_timer(timers, 2, trace, live, destroyed));
    co_await async::await_yield_once();
    check(timers.pending_count() == 2 && live == 2, "two child registrations and backing coexist");
    trace.push_back(3);
    released = true;
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    check(
        live == 0 && destroyed == 2,
        "ordinary close waits for both unobserved timers and releases backing"
    );
    co_return async::Completion<void>::success();
}

auto siblings() noexcept -> void {
    auto timers = TimerQueue();
    auto trace = std::vector<int>();
    auto live = 0;
    auto destroyed = 0;
    auto released = false;
    auto pump = PollGate(timers, released);
    auto result = async::drive_root(siblings_body(timers, trace, live, destroyed, released), &pump);
    check(
        result.success_if() != nullptr && live == 0 && destroyed == 2,
        "child backing destroyed once after close"
    );
    check(
        trace.size() == 5 && trace[0] == 1 && trace[1] == 2 && trace[2] == 3,
        "child startup and parent yield preserve FIFO"
    );
    check(
        (trace[3] == 11 && trace[4] == 12) || (trace[3] == 12 && trace[4] == 11),
        "each sibling completes once without a deadline ordering assumption"
    );
    check(timers.registration_count() == 2, "both children registered");
    balanced(timers);
}

auto pre_cancel_body(
    TimerQueue& timers,
    Observation& observed,
    TimerQueue::Clock::duration delay
) noexcept -> async::Operation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(timer(timers, delay, observed));
    async::cancel(child);
    auto result = co_await child.observe();
    check(result.is_cancelled(), "cancel before start is accepted at timer consumption");
    check(
        !async::cancellation_requested(),
        "child cancellation does not request parent cancellation"
    );
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<void>::success();
}

auto pre_cancel() noexcept -> void {
    for (const auto delay :
         {TimerQueue::Clock::duration(1h), TimerQueue::Clock::duration::zero()}) {
        auto timers = TimerQueue();
        auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
        auto result = async::drive_root(pre_cancel_body(timers, observed, delay), &timers);
        check(
            result.success_if() != nullptr
                && observed.started == 1
                && observed.finished == 1
                && observed.cancelled,
            "pre-cancelled child completes once, including nonpositive duration"
        );
        check(
            timers.registration_count() == 0 && timers.wait_count() == 0,
            "pre-cancelled timer never registers or waits"
        );
        balanced(timers);
    }
}

class Backing final {
    TimerQueue& timers;
    bool& alive;
    int& destroyed;

public:
    Backing(TimerQueue& provider, bool& state, int& destructions) noexcept
        : timers(provider),
          alive(state),
          destroyed(destructions) {
        alive = true;
    }

    ~Backing() noexcept {
        check(
            timers.pending_count() == 0,
            "close unlinks timers before releasing borrowed backing"
        );
        alive = false;
        ++destroyed;
    }
};

auto borrowed_timer(TimerQueue& timers, bool& alive, Observation& observed) noexcept
    -> async::Operation<void> {
    check(alive, "descendant backing alive at registration");
    auto result = co_await timer(timers, 1h, observed);
    check(alive, "descendant backing alive at cancellation delivery");
    co_return result;
}

auto ancestor(
    TimerQueue& timers,
    bool& alive,
    int& destroyed,
    Observation& own,
    Observation& descendant
) noexcept -> async::Operation<void> {
    auto backing = Backing(timers, alive, destroyed);
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(borrowed_timer(timers, alive, descendant));
    auto result = co_await timer(timers, 1h, own);
    check(
        result.is_cancelled() && async::cancellation_requested(),
        "registered ancestor accepts requested cancellation"
    );
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    check(
        descendant.cancelled && descendant.finished == 1,
        "descendant inherits ancestor cancellation before close"
    );
    co_return result;
}

auto cancelled_close_body(
    TimerQueue& timers,
    bool& alive,
    int& destroyed,
    Observation& own,
    Observation& descendant
) noexcept -> async::Operation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(ancestor(timers, alive, destroyed, own, descendant));
    co_await async::await_yield_once();
    co_await async::await_yield_once();
    check(
        timers.pending_count() == 2 && alive,
        "ancestor and descendant register before cancellation"
    );
    async::cancel(child);
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    check(
        !alive && destroyed == 1 && !async::cancellation_requested(),
        "unobserved child closes once and restores parent context"
    );
    co_return async::Completion<void>::success();
}

auto cancelled_close() noexcept -> void {
    auto timers = TimerQueue();
    auto alive = false;
    auto destroyed = 0;
    auto own = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto descendant = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result =
        async::drive_root(cancelled_close_body(timers, alive, destroyed, own, descendant), &timers);
    check(
        result.success_if() != nullptr && own.finished == 1 && own.cancelled,
        "registered cancellation and close complete once"
    );
    check(
        timers.registration_count() == 2 && timers.wait_count() == 0,
        "cancelled one-hour timers do not wait for deadline"
    );
    balanced(timers);
}

auto committed_body(TimerQueue& timers, Observation& observed, bool& released) noexcept
    -> async::Operation<void> {
    auto& activation = co_await async::current_activation();
    auto scope = async::ChildScope(activation);
    auto child = scope.start(timer(timers, 2ms, observed));
    co_await async::await_yield_once();
    check(timers.pending_count() == 1, "child registers before explicit timer commit");
    check(timers.wait(), "registered timer has a real deadline");
    timers.poll(*activation.task->driver);
    check(
        timers.pending_count() == 0 && timers.completion_count() == 1 && observed.finished == 0,
        "provider commits and unlinks before dispatching child continuation"
    );
    async::cancel(child);
    released = true;
    auto result = co_await child.observe();
    check(
        result.success_if() != nullptr && !observed.cancelled && observed.finished == 1,
        "cancel after committed readiness cannot rewrite success"
    );
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<void>::success();
}

auto committed() noexcept -> void {
    auto timers = TimerQueue();
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto released = false;
    auto pump = PollGate(timers, released);
    auto result = async::drive_root(committed_body(timers, observed, released), &pump);
    check(result.success_if() != nullptr, "committed timer returns success");
    balanced(timers);
}

auto rearm_body(TimerQueue& timers) noexcept -> async::Operation<void> {
    auto first = co_await timers.wait_for(2ms);
    check(
        first.success_if() != nullptr && timers.pending_count() == 0,
        "first awaiter can die after unlink"
    );
    auto second = co_await timers.wait_for(2ms);
    check(
        second.success_if() != nullptr && timers.pending_count() == 0,
        "continuation can register and finish another timer"
    );
    co_return second;
}

auto rearm() noexcept -> void {
    auto timers = TimerQueue();
    auto result = async::drive_root(rearm_body(timers), &timers);
    check(
        result.success_if() != nullptr && timers.registration_count() == 2,
        "sequential timer reuse completes twice"
    );
    balanced(timers);
}

} // namespace

auto main() -> int {
    for (const auto& test : {
             std::pair {"cold", cold},
             std::pair {"immediate", immediate},
             std::pair {"future", future},
             std::pair {"siblings", siblings},
             std::pair {"pre-cancel", pre_cancel},
             std::pair {"cancelled-close", cancelled_close},
             std::pair {"committed", committed},
             std::pair {"rearm", rearm},
         }) {
        test.second();
        std::printf("%s: pass\n", test.first);
    }
    return EXIT_SUCCESS;
}
