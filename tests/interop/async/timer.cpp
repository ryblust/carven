#include "timer.hpp"

#include <carven/runtime/execution/child.hpp>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>
#include <vector>

namespace {

namespace async = carven::runtime::execution;
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
    -> async::AsyncOperation<void> {
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
) noexcept -> async::AsyncOperation<void> {
    check(capture != nullptr, "cold capture remains owned when body starts");
    co_return co_await timer(timers, 1h, observed);
}

auto cold() noexcept -> void {
    auto timers = TimerQueue();
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto live = 0;
    auto destroyed = 0;
    {
        const auto operation =
            captured_timer(timers, std::make_unique<Lifetime>(live, destroyed), observed);
        check(live == 1 && destroyed == 0, "unstarted operation owns capture");
        check(observed.started == 0, "construction does not start timer");
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
        balanced(timers);
    }
}

// Delay the first provider poll until the driver is idle. This establishes a
// real idle wait even if the process was descheduled beyond the deadline.
class IdleWait final : public async::EventSource {
    TimerQueue& timers;
    bool waited = false;
    std::optional<TimerQueue::Clock::time_point> deadline;

public:
    explicit IdleWait(TimerQueue& provider) noexcept
        : timers(provider) {}

    auto poll(async::Driver& driver) noexcept -> void override {
        if (waited || timers.pending_count() == 0) {
            timers.poll(driver);
        }
    }

    auto wait(std::optional<std::int64_t> limit) noexcept -> bool override {
        check(timers.pending_count() == 1, "future timer suspends before idle wait");
        deadline = timers.next_deadline();
        check(deadline.has_value(), "registered timer exposes its actual deadline");
        waited = true;
        return timers.wait(limit);
    }

    auto completed_after_deadline() const noexcept -> bool {
        return deadline && TimerQueue::Clock::now() >= *deadline;
    }
};

auto future() noexcept -> void {
    auto timers = TimerQueue();
    auto source = IdleWait(timers);
    auto observed = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result = async::drive_root(timer(timers, 2ms, observed), &source);
    check(
        result.success_if() != nullptr && observed.finished == 1,
        "future timer delivers success once"
    );
    check(source.completed_after_deadline(), "future timer never completes before deadline");
    balanced(timers);
}

// Hold provider delivery while a parent establishes the registered boundary.
// Timers keep real deadlines; only delivery policy is gated on the same thread.
class PollGate final : public async::EventSource {
    TimerQueue& timers;
    bool& released;

public:
    PollGate(TimerQueue& provider, bool& allow_poll) noexcept
        : timers(provider),
          released(allow_poll) {}

    auto poll(async::Driver& driver) noexcept -> void override {
        if (released) {
            timers.poll(driver);
        }
    }

    auto wait(std::optional<std::int64_t> limit) noexcept -> bool override {
        return timers.wait(limit);
    }
};

auto sibling_timer(
    TimerQueue& timers,
    int number,
    std::vector<int>& trace,
    int& live,
    int& destroyed
) noexcept -> async::AsyncOperation<void> {
    const auto lifetime = Lifetime(live, destroyed);
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
) noexcept -> async::AsyncOperation<void> {
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
    auto source = PollGate(timers, released);
    auto result =
        async::drive_root(siblings_body(timers, trace, live, destroyed, released), &source);
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
    balanced(timers);
}

auto pre_cancel_body(
    TimerQueue& timers,
    Observation& observed,
    TimerQueue::Clock::duration delay
) noexcept -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(timer(timers, delay, observed));
    async::cancel(child);
    const auto result = co_await child.observe();
    check(result.is_cancelled(), "cancel before start is accepted at timer consumption");
    check(!async::cancel_requested(), "child cancellation does not request parent cancellation");
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

auto borrowed_timer(TimerQueue& timers, const bool& alive, Observation& observed) noexcept
    -> async::AsyncOperation<void> {
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
) noexcept -> async::AsyncOperation<void> {
    const auto backing = Backing(timers, alive, destroyed);
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(borrowed_timer(timers, alive, descendant));
    auto result = co_await timer(timers, 1h, own);
    check(
        result.is_cancelled() && async::cancel_requested(),
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
) noexcept -> async::AsyncOperation<void> {
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
        !alive && destroyed == 1 && !async::cancel_requested(),
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
    balanced(timers);
}

auto committed_body(TimerQueue& timers, Observation& observed, bool& released) noexcept
    -> async::AsyncOperation<void> {
    auto& activation = co_await async::current_activation();
    auto scope = async::ChildScope(activation);
    auto child = scope.start(timer(timers, 2ms, observed));
    co_await async::await_yield_once();
    check(timers.pending_count() == 1, "child registers before explicit timer commit");
    check(timers.wait(std::nullopt), "registered timer has a real deadline");
    timers.poll(*activation.task->driver);
    check(
        timers.pending_count() == 0 && observed.finished == 0,
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
    auto source = PollGate(timers, released);
    auto result = async::drive_root(committed_body(timers, observed, released), &source);
    check(result.success_if() != nullptr, "committed timer returns success");
    balanced(timers);
}

auto rearm_body(TimerQueue& timers) noexcept -> async::AsyncOperation<void> {
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
    check(result.success_if() != nullptr, "sequential timer reuse completes twice");
    balanced(timers);
}

class WaitGate final : public async::EventSource {
    TimerQueue& timers;
    bool& released;

public:
    WaitGate(TimerQueue& provider, bool& allow_poll) noexcept
        : timers(provider),
          released(allow_poll) {}

    auto poll(async::Driver& driver) noexcept -> void override { timers.poll(driver); }

    auto wait(std::optional<std::int64_t> limit) noexcept -> bool override {
        released = true;
        return timers.wait(limit);
    }
};

auto multiple_sources(
    TimerQueue& fast,
    TimerQueue& slow,
    WaitGate& slow_source,
    Observation& first,
    Observation& second
) noexcept -> async::AsyncOperation<void> {
    auto& activation = co_await async::current_activation();
    auto& driver = *activation.task->driver;
    driver.attach(slow_source);
    auto scope = async::ChildScope(activation);
    auto fast_child = scope.start(timer(fast, 2ms, first));
    auto slow_child = scope.start(timer(slow, 1h, second));
    const auto selected = co_await async::wait_ready(fast_child, slow_child);
    check(
        selected.head == 0,
        "independent fast source makes progress while slow source is pending"
    );
    const auto result = co_await fast_child.observe();
    check(result.success_if() != nullptr, "fast source publishes success");
    async::cancel(slow_child);
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    check(
        first.finished == 1 && second.finished == 1 && second.cancelled,
        "source cancellation closes both children before detach"
    );
    driver.detach(slow_source);
    co_return async::Completion<void>::success();
}

auto multi_source() noexcept -> void {
    auto fast = TimerQueue();
    auto slow = TimerQueue();
    auto released = false;
    auto fast_source = PollGate(fast, released);
    auto slow_source = WaitGate(slow, released);
    auto first = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto second = Observation {.started = 0, .finished = 0, .cancelled = false};
    auto result =
        async::drive_root(multiple_sources(fast, slow, slow_source, first, second), &fast_source);
    check(result.success_if() != nullptr, "multiple event sources preserve root completion");
    balanced(fast);
    balanced(slow);
}

} // namespace

auto run_timer_cases() noexcept -> void {
    for (const auto& test : {
             std::pair {"cold", cold},
             std::pair {"immediate", immediate},
             std::pair {"future", future},
             std::pair {"siblings", siblings},
             std::pair {"pre-cancel", pre_cancel},
             std::pair {"cancelled-close", cancelled_close},
             std::pair {"committed", committed},
             std::pair {"rearm", rearm},
             std::pair {"multi-source", multi_source},
         }) {
        test.second();
        std::printf("%s: pass\n", test.first);
    }
}
