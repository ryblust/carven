module;
#include <carven/runtime/execution/execution.hpp>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

module carven:test.internal.runtime.execution;

import :test.harness.framework;

namespace {
namespace async = carven::runtime::execution;

struct DeadlinePump final {
    int waits = 0;
    int bounded_waits = 0;
    async::TaskContext** cancel_at_wait = nullptr;
    bool cancel_in_poll = false;

    auto poll(async::Driver& driver) noexcept -> void {
        if (cancel_in_poll
            && cancel_at_wait != nullptr
            && *cancel_at_wait != nullptr
            && driver.timer_queue().next_deadline().has_value()) {
            (*cancel_at_wait)->request_cancel();
            cancel_in_poll = false;
        }
    }

    auto wait() noexcept -> bool {
        ++waits;
        return false;
    }

    auto wait_until(std::int64_t deadline) noexcept -> bool {
        ++bounded_waits;
        if (cancel_at_wait != nullptr) {
            (*cancel_at_wait)->request_cancel();
            return true;
        }
        async::detail::steady_wait_until(deadline);
        return true;
    }
};

class Fixed final {
public:
    int value;
    const Fixed* identity;

    explicit Fixed(int source) noexcept;

    Fixed(const Fixed&) = delete;
    Fixed(Fixed&&) = delete;
};

Fixed::Fixed(int source) noexcept
    : value(source),
      identity(this) {}

static_assert(std::is_move_constructible_v<async::Completion<Fixed>>);
static_assert(!std::is_copy_constructible_v<async::Completion<Fixed>>);
static_assert(std::is_nothrow_move_assignable_v<async::AsyncOperation<int>>);
static_assert(!std::is_move_constructible_v<async::Child<int>>);

auto deep(int depth, int& bodies) noexcept -> async::AsyncOperation<int> {
    ++bodies;
    if (depth == 0) {
        co_return async::Completion<int>::success(0);
    }
    auto next = co_await deep(depth - 1, bodies);
    co_return async::Completion<int>::success(next.success_if()->value + 1);
}

auto deep_close(int depth, int& bodies) noexcept -> async::AsyncOperation<void> {
    ++bodies;
    if (depth != 0) {
        auto scope = async::ChildScope(co_await async::current_activation());
        auto child = scope.start(deep_close(depth - 1, bodies));
        co_await scope.close(async::ClosingPolicy::RequestCancelAndClose);
    }
    co_return async::Completion<void>::success();
}

auto fixed(int& factories) noexcept -> async::AsyncOperation<Fixed> {
    co_return async::Completion<Fixed>::success_from([&]() noexcept {
        ++factories;
        return Fixed(42);
    });
}

auto numbered(std::vector<int>& log, int number) noexcept -> async::AsyncOperation<void> {
    log.push_back(number);
    auto yielded = co_await async::yield_once();
    if (yielded.success_if() == nullptr) {
        co_return async::Completion<void>::cancelled();
    }
    log.push_back(number + 10);
    co_return async::Completion<void>::success();
}

auto fifo(std::vector<int>& log) noexcept -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto first = scope.start(numbered(log, 1));
    auto second = scope.start(numbered(log, 2));
    auto first_result = co_await first.observe();
    auto second_result = co_await second.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return first_result.success_if() != nullptr&& second_result.success_if() != nullptr
        ? async::Completion<void>::success()
        : async::Completion<void>::cancelled();
}

auto nested_checkpoint(bool& same_task, async::AsyncOperation<void> checkpoint) noexcept
    -> async::AsyncOperation<void> {
    same_task = async::cancel_requested();
    co_return co_await async::AsyncOperation<void>(std::move(checkpoint));
}

auto accepting(
    bool& yield_succeeded,
    bool& same_task,
    async::AsyncOperation<void> checkpoint
) noexcept -> async::AsyncOperation<void> {
    auto turn = async::yield_once();
    auto yielded = co_await async::AsyncOperation<void>(std::move(turn));
    yield_succeeded = yielded.success_if() != nullptr && async::cancel_requested();
    co_return co_await nested_checkpoint(same_task, std::move(checkpoint));
}

auto cancel_child(bool& yield_succeeded, bool& same_task, bool& parent_unrequested) noexcept
    -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto checkpoint = async::cancel_point();
    auto child = scope.start(accepting(yield_succeeded, same_task, std::move(checkpoint)));
    async::cancel(child);
    async::cancel(child);
    parent_unrequested = !async::cancel_requested();
    auto completion = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return completion;
}

auto sibling(bool& released, int& finished) noexcept -> async::AsyncOperation<void> {
    while (!released) {
        auto result = co_await async::yield_once();
        if (result.success_if() == nullptr) {
            co_return async::Completion<void>::cancelled();
        }
    }
    ++finished;
    co_return async::Completion<void>::success();
}

auto immediate() noexcept -> async::AsyncOperation<int> {
    co_return async::Completion<int>::success(7);
}

auto local_child_scope() noexcept -> async::AsyncOperation<int> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(immediate());
    auto result = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return result;
}

auto separate_owners(int& finished) noexcept -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto released = false;
    auto child = scope.start(sibling(released, finished));
    auto result = co_await local_child_scope();
    released = result.success_if() != nullptr;
    auto child_result = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return child_result;
}

class Backing final {
public:
    bool* alive;
    std::vector<int>* log;

    Backing(bool& state, std::vector<int>& events) noexcept;
    ~Backing() noexcept;
};

Backing::Backing(bool& state, std::vector<int>& events) noexcept
    : alive(&state),
      log(&events) {
    *alive = true;
}

Backing::~Backing() noexcept {
    log->push_back(3);
    *alive = false;
}

class Abandoned final {
public:
    bool* alive;
    std::vector<int>* log;
    int destruction_event;

    Abandoned(bool& state, std::vector<int>& events, int event = 2) noexcept;

    Abandoned(const Abandoned&) = delete;
    Abandoned(Abandoned&&) = delete;

    ~Abandoned() noexcept;
};

Abandoned::Abandoned(bool& state, std::vector<int>& events, int event) noexcept
    : alive(&state),
      log(&events),
      destruction_event(event) {}

Abandoned::~Abandoned() noexcept {
    log->push_back(*alive ? destruction_event : -destruction_event);
}

auto grandchild(bool& alive, std::vector<int>& log) noexcept -> async::AsyncOperation<Abandoned> {
    auto yielded = co_await async::yield_once();
    log.push_back(yielded.success_if() != nullptr && alive ? 1 : -1);
    co_return async::Completion<Abandoned>::success_from([&]() noexcept {
        return Abandoned(alive, log);
    });
}

auto closing_parent(bool& alive, std::vector<int>& log) noexcept -> async::AsyncOperation<void> {
    auto backing = Backing(alive, log);
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(grandchild(alive, log));
    auto checkpoint = co_await async::cancel_point();
    // Cancellation selects an outcome; lexical closure still drains the child.
    co_await scope.close(async::ClosingPolicy::RequestCancelAndClose);
    co_return checkpoint;
}

auto nested_close(bool& alive, std::vector<int>& log) noexcept -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(closing_parent(alive, log));
    async::cancel(child);
    auto completion = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return completion;
}

auto unobserved_sibling(bool& alive, std::vector<int>& log, int number, int turns) noexcept
    -> async::AsyncOperation<Abandoned> {
    log.push_back(alive ? number : -number);
    for (auto turn = 0; turn < turns; ++turn) {
        co_await async::yield_once();
    }
    log.push_back(alive ? number + 10 : -(number + 10));
    co_return async::Completion<Abandoned>::success_from([&]() noexcept {
        return Abandoned(alive, log, number + 20);
    });
}

auto close_siblings(bool& alive, std::vector<int>& log, int first_turns, int second_turns) noexcept
    -> async::AsyncOperation<void> {
    auto backing = Backing(alive, log);
    auto scope = async::ChildScope(co_await async::current_activation());
    auto first = scope.start(unobserved_sibling(alive, log, 1, first_turns));
    auto second = scope.start(unobserved_sibling(alive, log, 2, second_turns));
    co_await scope.close(async::ClosingPolicy::RequestCancelAndClose);
    log.push_back(alive ? 9 : -9);
    co_return async::Completion<void>::success();
}

auto cancelled_before_close(std::vector<int>& log) noexcept -> async::AsyncOperation<void> {
    auto result = co_await async::cancel_point();
    log.push_back(result.is_cancelled() ? 1 : -1);
    co_return result;
}

auto mark(std::vector<int>& log) noexcept -> async::AsyncOperation<void> {
    log.push_back(7);
    co_return async::Completion<void>::success();
}

auto close_once(std::vector<int>& log) noexcept -> async::AsyncOperation<void> {
    auto outer = async::ChildScope(co_await async::current_activation());
    auto inner = async::ChildScope(co_await async::current_activation());
    auto cancelled = inner.start(cancelled_before_close(log));
    co_await inner.close(async::ClosingPolicy::RequestCancelAndClose);
    log.push_back(2);
    auto queued = outer.start(mark(log));
    co_await inner.close(async::ClosingPolicy::CloseOnly);
    log.push_back(3);
    auto completion = co_await queued.observe();
    log.push_back(4);
    co_await outer.close(async::ClosingPolicy::CloseOnly);
    co_return completion;
}

auto record(std::vector<int>& log, int number) noexcept -> async::AsyncOperation<void> {
    log.push_back(number);
    co_return async::Completion<void>::success();
}

auto yielding_dependency(std::vector<int>& log) noexcept -> async::AsyncOperation<void> {
    log.push_back(1);
    auto ready = co_await record(log, 2);
    auto turn = async::yield_once();
    log.push_back(3);
    auto yielded = co_await async::AsyncOperation<void>(std::move(turn));
    log.push_back(4);
    co_return ready.success_if() != nullptr&& yielded.success_if() != nullptr
        ? async::Completion<void>::success()
        : async::Completion<void>::cancelled();
}

auto explicit_boundary(std::vector<int>& log) noexcept -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(record(log, 8));
    auto result = co_await yielding_dependency(log);
    log.push_back(9);
    auto next = scope.start(record(log, 7));
    auto observed = co_await child.observe();
    log.push_back(10);
    auto next_observed = co_await next.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return result.success_if() != nullptr&& observed.success_if()
            != nullptr&& next_observed.success_if() != nullptr
        ? async::Completion<void>::success()
        : async::Completion<void>::cancelled();
}

auto inherited_checkpoint(bool& inherited) noexcept -> async::AsyncOperation<void> {
    inherited = async::cancel_requested();
    co_return co_await async::cancel_point();
}

auto cancelled_context(bool& before, bool& after, bool& inherited) noexcept
    -> async::AsyncOperation<void> {
    before = async::cancel_requested();
    auto yielded = co_await async::yield_once();
    after = yielded.success_if() != nullptr && async::cancel_requested();
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(inherited_checkpoint(inherited));
    auto result = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return result;
}

auto unrequested_context(bool& before, bool& after) noexcept -> async::AsyncOperation<void> {
    before = !async::cancel_requested();
    auto yielded = co_await async::yield_once();
    after = yielded.success_if() != nullptr && !async::cancel_requested();
    co_return async::Completion<void>::success();
}

auto sibling_contexts(bool& restored, bool& inherited) noexcept -> async::AsyncOperation<void> {
    auto cancelled_before = false;
    auto cancelled_after = false;
    auto sibling_before = false;
    auto sibling_after = false;
    auto scope = async::ChildScope(co_await async::current_activation());
    auto cancelled = scope.start(cancelled_context(cancelled_before, cancelled_after, inherited));
    auto sibling = scope.start(unrequested_context(sibling_before, sibling_after));
    async::cancel(cancelled);
    auto cancelled_result = co_await cancelled.observe();
    auto sibling_result = co_await sibling.observe();
    restored = cancelled_before
        && cancelled_after
        && sibling_before
        && sibling_after
        && cancelled_result.is_cancelled()
        && sibling_result.success_if() != nullptr
        && !async::cancel_requested();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<void>::success();
}

class Capture final {
    bool* alive;
    std::vector<int>* log;
    bool owns;

public:
    Capture(bool& state, std::vector<int>& events) noexcept;
    Capture(const Capture&) = delete;
    Capture(Capture&& source) noexcept;
    ~Capture() noexcept;
};

Capture::Capture(bool& state, std::vector<int>& events) noexcept
    : alive(&state),
      log(&events),
      owns(true) {
    *alive = true;
}

Capture::Capture(Capture&& source) noexcept
    : alive(source.alive),
      log(source.log),
      owns(std::exchange(source.owns, false)) {}

Capture::~Capture() noexcept {
    if (owns) {
        log->push_back(3);
        *alive = false;
    }
}

auto captured_result(
    [[maybe_unused]] Capture capture,
    std::vector<int>& log,
    bool deferred
) noexcept -> async::AsyncOperation<Fixed> {
    log.push_back(1);
    if (deferred) {
        auto yielded = co_await async::yield_once();
        if (yielded.success_if() == nullptr) {
            co_return async::Completion<Fixed>::cancelled();
        }
    }
    co_return async::Completion<Fixed>::success_from([] static noexcept { return Fixed(42); });
}

auto inspect_completion(
    async::Completion<Fixed> completion,
    const bool& alive,
    std::vector<int>& log
) noexcept -> async::Completion<Fixed> {
    log.push_back(alive && completion.success_if() != nullptr ? 2 : -2);
    return completion;
}

auto captured_delivery(bool& alive, std::vector<int>& log, bool deferred) noexcept
    -> async::AsyncOperation<Fixed> {
    auto cold = captured_result(Capture(alive, log), log, deferred);
    auto moved = async::AsyncOperation<Fixed>(std::move(cold));
    if (!alive || !log.empty()) {
        co_return async::Completion<Fixed>::cancelled();
    }
    auto result =
        inspect_completion(co_await async::AsyncOperation<Fixed>(std::move(moved)), alive, log);
    log.push_back(4);
    co_return result;
}

class Tracked final {
public:
    int* moves;
    int* destructions;
    int value;

    Tracked(int& transfers, int& endings) noexcept;

    Tracked(const Tracked&) = delete;

    Tracked(Tracked&& source) noexcept;
    ~Tracked() noexcept;
};

Tracked::Tracked(int& transfers, int& endings) noexcept
    : moves(&transfers),
      destructions(&endings),
      value(17) {}

Tracked::Tracked(Tracked&& source) noexcept
    : moves(source.moves),
      destructions(source.destructions),
      value(source.value) {
    ++*moves;
}

Tracked::~Tracked() noexcept {
    ++*destructions;
}

auto tracked(int& moves, int& destructions) noexcept -> async::AsyncOperation<Tracked> {
    co_return async::Completion<Tracked>::success_from([&]() noexcept {
        return Tracked(moves, destructions);
    });
}

auto deadline_wait() noexcept -> async::AsyncOperation<bool> {
    const auto deadline = async::now() + 1'000'000;
    const auto completion = co_await async::sleep_until(deadline);
    const auto elapsed = async::now() >= deadline;
    const auto ready = co_await async::sleep_until(deadline);
    co_return async::Completion<bool>::success(
        completion.success_if() != nullptr && elapsed && ready.success_if() != nullptr
    );
}

auto host_cancelled_timer(async::TaskContext*& task) noexcept -> async::AsyncOperation<bool> {
    task = (co_await async::current_activation()).task;
    const auto result = co_await async::sleep_until(std::numeric_limits<std::int64_t>::max());
    co_return async::Completion<bool>::success(result.is_cancelled());
}

auto timer_descendant(std::int64_t deadline) noexcept -> async::AsyncOperation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto timer = scope.start(async::sleep_until(deadline));
    auto result = co_await timer.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return result;
}

auto cancel_ancestor_timer() noexcept -> async::AsyncOperation<bool> {
    auto& activation = co_await async::current_activation();
    auto scope = async::ChildScope(activation);
    auto child = scope.start(timer_descendant(async::now() + 1'000'000'000));
    co_await async::await_yield_once();
    co_await async::await_yield_once();
    const auto registered = activation.task->driver->timer_queue().next_deadline().has_value();
    async::cancel(child);
    async::cancel(child);
    const auto selected = co_await async::wait_ready(child);
    const auto unlinked = !activation.task->driver->timer_queue().next_deadline().has_value();
    co_await async::close(child);
    co_await async::close(child);
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<bool>::success(registered && unlinked && selected.tail.head);
}

auto cancel_unregistered_timer() noexcept -> async::AsyncOperation<bool> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(async::sleep_until(async::now() + 1'000'000'000));
    async::cancel(child);
    const auto result = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<bool>::success(result.is_cancelled());
}

auto readiness_value(int turns, int value) noexcept -> async::AsyncOperation<int> {
    for (auto turn = 0; turn < turns; ++turn) {
        co_await async::yield_once();
    }
    co_return async::Completion<int>::success(value);
}

auto composition_candidate(int turns, int value, int outcome, std::vector<int>& started) noexcept
    -> async::AsyncOperation<int, int> {
    started.push_back(value);
    for (auto turn = 0; turn < turns; ++turn) {
        co_await async::await_yield_once();
    }
    if (outcome == 1) {
        co_return async::Completion<int, int>::failure(int(value));
    }
    if (outcome == 2) {
        co_return async::Completion<int, int>::cancelled();
    }
    co_return async::Completion<int, int>::success(value);
}

auto composition_character(char32_t value) noexcept -> async::AsyncOperation<char32_t> {
    co_return async::Completion<char32_t>::success(value);
}

struct CompositionTiming final {
    async::TaskContext* parent = nullptr;
    bool published = false;
    bool committed = false;
    bool after_commit;
    int waits = 0;

    auto poll(async::Driver&) noexcept -> void {
        if (parent != nullptr && !parent->requested && (after_commit ? committed : published)) {
            parent->request_cancel();
        }
    }

    auto wait() noexcept -> bool {
        ++waits;
        return false;
    }

    auto wait_until(std::int64_t) noexcept -> bool {
        ++waits;
        parent->request_cancel();
        return true;
    }
};

class CompositionCommit final {
    bool* committed;

public:
    explicit CompositionCommit(bool& flag) noexcept
        : committed(&flag) {}

    CompositionCommit(const CompositionCommit&) = delete;

    CompositionCommit(CompositionCommit&& source) noexcept
        : committed(source.committed) {
        *committed = true;
    }
};

auto composition_timing_winner(CompositionTiming& timing) noexcept
    -> async::AsyncOperation<CompositionCommit> {
    timing.published = true;
    co_return async::Completion<CompositionCommit>::success_from([&]() noexcept {
        return CompositionCommit(timing.committed);
    });
}

auto composition_timing_loser(CompositionTiming& timing) noexcept
    -> async::AsyncOperation<CompositionCommit> {
    const auto timer = co_await async::sleep_until(std::numeric_limits<std::int64_t>::max());
    if (timer.is_cancelled()) {
        co_return async::Completion<CompositionCommit>::cancelled();
    }
    co_return async::Completion<CompositionCommit>::success_from([&]() noexcept {
        return CompositionCommit(timing.committed);
    });
}

auto composition_timing(CompositionTiming& timing) noexcept -> async::AsyncOperation<bool> {
    timing.parent = (co_await async::current_activation()).task;
    using Product = carven::runtime::Tuple<std::size_t, CompositionCommit>;
    auto result = co_await async::when_any<Product>(
        composition_timing_winner(timing),
        composition_timing_loser(timing)
    );
    const auto correct = timing.after_commit
        ? result.success_if() != nullptr && result.success_if()->value.head == 0 && timing.committed
        : result.is_cancelled() && !timing.committed;
    const auto closed = !timing.parent->driver->timer_queue().next_deadline().has_value();
    co_return async::Completion<bool>::success(correct && closed && timing.parent->requested);
}

auto composition_until_cancel(bool& closed, std::vector<int>& started) noexcept
    -> async::AsyncOperation<int> {
    started.push_back(1);
    while (!async::cancel_requested()) {
        co_await async::await_yield_once();
    }
    closed = true;
    co_return async::Completion<int>::cancelled();
}

auto composition_ambient(
    async::TaskContext*& task,
    bool before_start,
    std::vector<int>& started
) noexcept -> async::AsyncOperation<bool> {
    task = (co_await async::current_activation()).task;
    if (before_start) {
        task->request_cancel();
        auto result = co_await async::when_all<carven::runtime::Tuple<int>, int>(
            composition_candidate(0, 7, 0, started)
        );
        co_return async::Completion<bool>::success(result.is_cancelled() && started.empty());
    }
    auto result = co_await async::when_all<carven::runtime::Tuple<carven::runtime::Void>>(
        async::sleep_until(std::numeric_limits<std::int64_t>::max())
    );
    const auto closed = !task->driver->timer_queue().next_deadline().has_value();
    co_return async::Completion<bool>::success(result.is_cancelled() && closed);
}

auto cancelled_signal() noexcept -> async::AsyncOperation<void> {
    co_return async::Completion<void>::cancelled();
}

auto failed_signal() noexcept -> async::AsyncOperation<void, int> {
    co_await async::yield_once();
    co_return async::Completion<void, int>::failure(17);
}

auto readiness_order() noexcept -> async::AsyncOperation<bool> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto first = scope.start(readiness_value(1, 7));
    auto second = scope.start(readiness_value(0, 9));
    const auto committed = co_await async::wait_ready(first, second);
    // First resumes before this parent, so both are now done. The original
    // second-child publication must remain the committed winner.
    const auto repeated = co_await async::wait_ready(first, second);
    auto one = co_await first.observe();
    auto two = co_await second.observe();
    co_await async::close(first);
    co_await async::close(first);
    co_await async::close(second);
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<bool>::success(
        committed.head == 1
        && !committed.tail.head
        && repeated.head == 0
        && !repeated.tail.head
        && one.success_if()->value == 7
        && two.success_if()->value == 9
    );
}

auto readiness_cancellation() noexcept -> async::AsyncOperation<bool> {
    auto& activation = co_await async::current_activation();
    activation.task->request_cancel();
    auto scope = async::ChildScope(activation);
    auto ignored = scope.start(readiness_value(1, 7));
    async::cancel(ignored);
    const auto successful = co_await async::wait_ready(ignored);
    auto success = co_await ignored.observe();
    auto cancelled = scope.start(cancelled_signal());
    const auto accepted = co_await async::wait_ready(cancelled);
    co_await async::close(cancelled);
    auto drained = scope.start(readiness_value(2, 9));
    co_await async::close(drained);
    co_await async::close(drained);
    const auto sticky = async::cancel_requested();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<bool>::success(
        successful.head == 0
        && !successful.tail.head
        && success.success_if()->value == 7
        && accepted.head == 0
        && accepted.tail.head
        && sticky
    );
}

auto readiness_storage(bool& alive, std::vector<int>& log) noexcept -> async::AsyncOperation<bool> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto fixed_child = scope.start(captured_result(Capture(alive, log), log, true));
    auto scalar_child = scope.start(readiness_value(0, 9));
    const auto selected = co_await async::wait_ready(fixed_child, scalar_child);
    const auto retained = alive;
    co_await async::close(fixed_child);
    const auto released = !alive && log == std::vector<int>({1, 3});
    auto scalar = co_await scalar_child.observe();
    auto failure = scope.start(failed_signal());
    co_await async::close(failure);
    co_await async::close(failure);
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return async::Completion<bool>::success(
        selected.head == 1
        && !selected.tail.head
        && retained
        && released
        && scalar.success_if()->value == 9
    );
}

const TestSuite suite([] static noexcept {
    "Runtime composition: ambient cancellation distinguishes publication from outward commit"_test =
        [] static noexcept {
            for (const auto after_commit : {false, true}) {
                auto timing = CompositionTiming {.after_commit = after_commit};
                auto result = async::drive_root(composition_timing(timing), &timing);
                if (expect(result.success_if() != nullptr)) {
                    expect(result.success_if()->value).note("after commit = ", after_commit);
                }
                expect(timing.published);
                expect_equal(timing.waits, 0);
            }
        };
    "Runtime composition: semantic Char validation is an explicit operand contract"_test =
        [] static noexcept {
            using Product = carven::runtime::Tuple<char32_t>;
            auto checked = async::drive_root(
                async::when_all<Product>(async::composition_operand<true>(
                    composition_character(U'\U0001D11E'),
                    carven::runtime::SourceSite::native()
                ))
            );
            if (expect(checked.success_if() != nullptr)) {
                expect(checked.success_if()->value.head == U'\U0001D11E');
            }
            // Native char32_t alone does not imply the source Char contract.
            auto unchecked = async::drive_root(
                async::when_all<Product>(composition_character(char32_t(0xd800)))
            );
            if (expect(unchecked.success_if() != nullptr)) {
                expect(unchecked.success_if()->value.head == char32_t(0xd800));
            }
        };
    "Runtime composition: cold ownership, ordered products, unit slots and empty join"_test =
        [] static noexcept {
            using Product = carven::runtime::Tuple<int, carven::runtime::Void>;
            auto started = std::vector<int>();
            {
                auto cold = async::when_all<Product, int>(
                    composition_candidate(0, 7, 0, started),
                    async::yield_once()
                );
            }
            expect(started.empty());
            auto result = async::drive_root(
                async::when_all<Product, int>(
                    composition_candidate(1, 7, 0, started),
                    async::yield_once()
                )
            );
            if (expect(result.success_if() != nullptr)) {
                expect_equal(result.success_if()->value.head, 7);
            }
            expect(started == std::vector<int>({7}));
            auto empty = async::drive_root(async::when_all<void>());
            expect(empty.success_if() != nullptr);
        };
    "Runtime composition: join stops siblings promptly and nominal failure overrides cancellation"_test =
        [] static noexcept {
            using Product = carven::runtime::Tuple<int, int>;
            auto started = std::vector<int>();
            auto closed = false;
            auto failed = async::drive_root(
                async::when_all<Product, int>(
                    composition_until_cancel(closed, started),
                    composition_candidate(0, 17, 1, started)
                )
            );
            expect(closed);
            expect(started == std::vector<int>({1, 17}));
            if (expect(failed.failure_if<int>() != nullptr)) {
                expect_equal(*failed.failure_if<int>(), 17);
            }
            started.clear();
            auto precedence = async::drive_root(
                async::when_all<Product, int>(
                    composition_candidate(0, 7, 2, started),
                    composition_candidate(1, 19, 1, started)
                )
            );
            if (expect(precedence.failure_if<int>() != nullptr)) {
                expect_equal(*precedence.failure_if<int>(), 19);
            }
        };
    "Runtime composition: races retain terminal choice and first-success fallback"_test =
        [] static noexcept {
            using Race = carven::runtime::Tuple<std::size_t, int>;
            auto started = std::vector<int>();
            auto any = async::drive_root(
                async::when_any<Race, int>(
                    composition_candidate(0, 7, 0, started),
                    composition_candidate(1, 19, 1, started)
                )
            );
            if (expect(any.success_if() != nullptr)) {
                expect_equal(any.success_if()->value.head, 0uz);
                expect_equal(any.success_if()->value.tail.head, 7);
            }
            auto successful = async::drive_root(
                async::first_successful<Race, int>(
                    composition_candidate(2, 11, 1, started),
                    composition_candidate(0, 22, 1, started),
                    composition_candidate(3, 7, 0, started)
                )
            );
            if (expect(successful.success_if() != nullptr)) {
                expect_equal(successful.success_if()->value.head, 2uz);
                expect_equal(successful.success_if()->value.tail.head, 7);
            }
            auto fallback = async::drive_root(
                async::first_successful<Race, int>(
                    composition_candidate(2, 11, 1, started),
                    composition_candidate(0, 22, 1, started)
                )
            );
            if (expect(fallback.failure_if<int>() != nullptr)) {
                expect_equal(*fallback.failure_if<int>(), 22);
            }
            auto cancelled = async::drive_root(
                async::first_successful<Race, int>(
                    composition_candidate(0, 7, 2, started),
                    composition_candidate(0, 9, 2, started)
                )
            );
            expect(cancelled.is_cancelled());
        };
    "Runtime composition: ambient cancellation wakes selection and closes providers"_test =
        [] static noexcept {
            auto* task = static_cast<async::TaskContext*>(nullptr);
            auto started = std::vector<int>();
            auto before = async::drive_root(composition_ambient(task, true, started));
            if (expect(before.success_if() != nullptr)) {
                expect(before.success_if()->value);
            }
            task = nullptr;
            auto pump = DeadlinePump();
            pump.cancel_at_wait = &task;
            auto waiting = async::drive_root(composition_ambient(task, false, started), &pump);
            if (expect(waiting.success_if() != nullptr)) {
                expect(waiting.success_if()->value);
            }
            expect_equal(pump.bounded_waits, 1);
        };
    "Async timers: ordered deadlines unlink before delivery and cancellation notification"_test =
        [] static noexcept {
            struct FakeClock final {
                std::int64_t instant = 0;
                int waits = 0;
            };
            auto clock = FakeClock();
            auto queue = async::detail::TimerQueue(
                {.state = &clock,
                 .now = [](
                            void* state
                        ) static noexcept { return static_cast<FakeClock*>(state)->instant; },
                 .wait_until =
                     [](void* state, std::int64_t deadline) static noexcept {
                         auto& clock = *static_cast<FakeClock*>(state);
                         clock.instant = deadline;
                         ++clock.waits;
                     }}
            );
            struct Receiver final {
                int identity;
                bool requested = false;
                std::vector<int>* events;
                async::detail::TimerRegistration* registration = nullptr;
            };
            auto events = std::vector<int>();
            auto receivers = std::array {
                Receiver {.identity = 1, .events = &events},
                Receiver {.identity = 2, .events = &events},
                Receiver {.identity = 3, .events = &events},
                Receiver {.identity = 4, .events = &events}
            };
            auto registrations = std::array<async::detail::TimerRegistration, 4> {};
            const auto deadlines = std::array<std::int64_t, 4> {30, 10, 20, 20};
            for (auto index = 0uz; index < registrations.size(); ++index) {
                auto& registration = registrations[index];
                receivers[index].registration = &registration;
                registration = {
                    .queue = nullptr,
                    .previous = nullptr,
                    .next = nullptr,
                    .deadline = deadlines[index],
                    .receiver = &receivers[index],
                    .cancel_requested =
                        [](void* receiver) static noexcept {
                            return static_cast<Receiver*>(receiver)->requested;
                        },
                    .complete =
                        [](void* receiver, bool cancelled) static noexcept {
                            const auto& value = *static_cast<Receiver*>(receiver);
                            expect(value.registration->queue == nullptr);
                            value.events->push_back(cancelled ? -value.identity : value.identity);
                        }
                };
                queue.insert(registration);
            }
            queue.poll(false);
            queue.poll(false);
            expect(events.empty());
            expect(queue.wait());
            expect_equal(clock.instant, std::int64_t(10));
            queue.poll(false);
            expect(events == std::vector<int>({2}));
            expect(queue.next_deadline() == 20);
            receivers[0].requested = true;
            // Cancellation wins even when the same poll reaches its deadline.
            clock.instant = 30;
            expect(events == std::vector<int>({2}));
            queue.poll(true);
            expect(events == std::vector<int>({2, -1, 3, 4}));
            expect(!queue.next_deadline().has_value());
            expect(!queue.wait());
            expect_equal(clock.waits, 1);
        };
    "Async timers: default and external roots share bounded deadline waits"_test =
        [] static noexcept {
            auto result = async::drive_root(deadline_wait());
            if (expect(result.success_if() != nullptr)) {
                expect(result.success_if()->value);
            }
            auto pump = DeadlinePump();
            auto* task = static_cast<async::TaskContext*>(nullptr);
            pump.cancel_at_wait = &task;
            auto external = async::drive_root(host_cancelled_timer(task), &pump);
            if (expect(external.success_if() != nullptr)) {
                expect(external.success_if()->value);
            }
            expect_equal(pump.waits, 0);
            expect_equal(pump.bounded_waits, 1);
            auto polling = DeadlinePump();
            task = nullptr;
            polling.cancel_at_wait = &task;
            polling.cancel_in_poll = true;
            auto cancelled = async::drive_root(host_cancelled_timer(task), &polling);
            if (expect(cancelled.success_if() != nullptr)) {
                expect(cancelled.success_if()->value);
            }
            expect_equal(polling.waits, 0);
            expect_equal(polling.bounded_waits, 0);
        };
    "Async timers: ancestor and pre-registration cancellation unlink without idle waits"_test =
        [] static noexcept {
            auto pump = DeadlinePump();
            auto inherited = async::drive_root(cancel_ancestor_timer(), &pump);
            if (expect(inherited.success_if() != nullptr)) {
                expect(inherited.success_if()->value);
            }
            auto before_start = async::drive_root(cancel_unregistered_timer(), &pump);
            if (expect(before_start.success_if() != nullptr)) {
                expect(before_start.success_if()->value);
            }
            expect_equal(pump.waits, 0);
            expect_equal(pump.bounded_waits, 0);
        };
    "Runtime async: readiness commits publication once and preserves child observation"_test =
        [] static noexcept {
            auto result = async::drive_root(readiness_order());
            if (expect(result.success_if() != nullptr)) {
                expect(result.success_if()->value);
            }
        };
    "Runtime async: readiness and close preserve pending ambient cancellation"_test =
        [] static noexcept {
            auto result = async::drive_root(readiness_cancellation());
            if (expect(result.success_if() != nullptr)) {
                expect(result.success_if()->value);
            }
        };
    "Runtime async: heterogeneous readiness and close retain then release immovable backing"_test =
        [] static noexcept {
            auto alive = false;
            auto log = std::vector<int>();
            auto result = async::drive_root(readiness_storage(alive, log));
            if (expect(result.success_if() != nullptr)) {
                expect(result.success_if()->value);
            }
            expect(!alive);
            expect(log == std::vector<int>({1, 3}));
        };
    "Runtime async: cold operations and deep direct awaits execute once"_test = [] static noexcept {
        auto bodies = 0;
        {
            auto discarded = deep(4, bodies);
        }
        expect_equal(bodies, 0);
        auto alive = false;
        auto log = std::vector<int>();
        {
            auto discarded = captured_result(Capture(alive, log), log, false);
            expect(alive);
            expect(log.empty());
        }
        expect(!alive);
        expect(log == std::vector<int>({3}));
        auto cold = deep(20000, bodies);
        auto moved = std::move(cold);
        expect_equal(bodies, 0);
        auto result = async::drive_root(std::move(moved));
        if (!expect(result.success_if() != nullptr)) {
            return;
        }
        expect_equal(result.success_if()->value, 20000);
        expect_equal(bodies, 20001);
    };
    "Runtime async: owner replacement destroys cold backing and transfers execution"_test =
        [] static noexcept {
            auto old_alive = false;
            auto new_alive = false;
            auto old_log = std::vector<int>();
            auto new_log = std::vector<int>();
            {
                auto destination = captured_result(Capture(old_alive, old_log), old_log, false);
                auto source = captured_result(Capture(new_alive, new_log), new_log, false);
                destination = std::move(source);
                expect(!old_alive);
                expect(old_log == std::vector<int>({3}));
                expect(new_alive);
                expect(new_log.empty());
                auto& alias = destination;
                destination = std::move(alias);
                source = std::move(destination);
                auto result = async::drive_root(std::move(source));
                expect(result.success_if() != nullptr);
                expect(!new_alive);
                expect(new_log == std::vector<int>({1, 3}));
            }
            expect(old_log == std::vector<int>({3}));
            expect(new_log == std::vector<int>({1, 3}));
        };
    "Runtime async: deep lexical close drains without recursive frame destruction"_test =
        [] static noexcept {
            auto bodies = 0;
            auto result = async::drive_root(deep_close(20000, bodies));
            expect(result.success_if() != nullptr);
            expect_equal(bodies, 20001);
        };
    "Runtime async: owning carriers preserve immovable factory payloads"_test = [] static noexcept {
        auto factories = 0;
        auto result = async::drive_root(fixed(factories));
        auto moved = std::move(result);
        if (!expect(moved.success_if() != nullptr)) {
            return;
        }
        expect_equal(moved.success_if()->value.value, 42);
        expect_equal(factories, 1);
        auto* identity = moved.success_if();
        auto adopted = async::Completion<Fixed, int>::adopt_success(std::move(moved));
        if (!expect(adopted.success_if() == identity)) {
            return;
        }
        expect(moved.success_if() == nullptr);
        expect_equal(adopted.success_if()->value.value, 42);
        expect(adopted.success_if()->value.identity == &adopted.success_if()->value);
        expect_equal(factories, 1);
        auto failure = async::Completion<int, int>::failure(9);
        expect(failure.success_if() == nullptr);
        if (expect(failure.failure_if<int>() != nullptr)) {
            expect_equal(*failure.failure_if<int>(), 9);
        }
        expect(async::Completion<void>::cancelled().is_cancelled());
    };
    "Runtime async: completed close preserves cancellation and does not defer again"_test =
        [] static noexcept {
            auto log = std::vector<int>();
            auto result = async::drive_root(close_once(log));
            expect(result.success_if() != nullptr);
            expect(log == std::vector<int>({1, 2, 3, 7, 4}));
        };
    "Runtime async: primitive carriers and exact-result adoption preserve values"_test =
        [] static noexcept {
            enum class Value { Selected };
            auto scalar = async::Completion<int>::success(42);
            auto moved = std::move(scalar);
            auto adopted = async::Completion<int, long>::adopt_success(std::move(moved));
            if (expect(adopted.success_if() != nullptr)) {
                expect_equal(adopted.success_if()->value, 42);
            }
            expect(scalar.success_if() == nullptr);
            expect(moved.success_if() == nullptr);
            auto enumeration = async::Completion<Value>::success(Value::Selected);
            auto moved_enumeration = std::move(enumeration);
            if (expect(moved_enumeration.success_if() != nullptr)) {
                expect(moved_enumeration.success_if()->value == Value::Selected);
            }
            auto empty = async::Completion<void>::success();
            auto adopted_empty = async::Completion<void, int>::adopt_success(std::move(empty));
            expect(adopted_empty.success_if() != nullptr);
            expect(empty.success_if() == nullptr);
        };
    "Runtime async: yield defers through the same FIFO loop"_test = [] static noexcept {
        auto log = std::vector<int>();
        auto result = async::drive_root(fifo(log));
        expect(result.success_if() != nullptr);
        expect(log == std::vector<int>({1, 2, 11, 12}));
    };
    "Runtime async: child cancellation is inherited by direct awaits and accepted only at checkpoints"_test =
        [] static noexcept {
            auto yielded = false;
            auto same_task = false;
            auto parent_unrequested = false;
            expect(!async::cancel_requested());
            auto result = async::drive_root(cancel_child(yielded, same_task, parent_unrequested));
            expect(result.is_cancelled());
            expect(yielded && same_task && parent_unrequested);
            expect(!async::cancel_requested());
        };
    "Runtime async: direct dependencies run until explicit yield and ready observation adds no turn"_test =
        [] static noexcept {
            auto log = std::vector<int>();
            auto result = async::drive_root(explicit_boundary(log));
            expect(result.success_if() != nullptr);
            expect(log == std::vector<int>({1, 2, 3, 8, 4, 9, 10, 7}));
        };
    "Runtime async: sibling task and lexical owner contexts restore across yield"_test =
        [] static noexcept {
            auto restored = false;
            auto inherited = false;
            auto result = async::drive_root(sibling_contexts(restored, inherited));
            expect(result.success_if() != nullptr);
            expect(restored && inherited);
            expect(!async::cancel_requested());
        };
    "Runtime async: callee publication never waits for caller siblings"_test = [] static noexcept {
        auto finished = 0;
        auto result = async::drive_root(separate_owners(finished));
        expect(result.success_if() != nullptr);
        expect_equal(finished, 1);
    };
    "Runtime async: cancelled closure destroys abandoned results before backing locals"_test =
        [] static noexcept {
            auto alive = false;
            auto log = std::vector<int>();
            auto result = async::drive_root(nested_close(alive, log));
            expect(result.is_cancelled());
            expect(!alive);
            expect(log == std::vector<int>({1, 2, 3}));
        };
    "Runtime async: closing unobserved siblings retains backing through staggered completion"_test =
        [] static noexcept {
            for (const auto first_turns : {3, 1}) {
                auto alive = false;
                auto log = std::vector<int>();
                auto result =
                    async::drive_root(close_siblings(alive, log, first_turns, 4 - first_turns));
                expect(result.success_if() != nullptr).note("first turns = ", first_turns);
                expect(!alive).note("first turns = ", first_turns);
                auto execution = std::vector<int>();
                for (const auto event : log) {
                    if (event != 21 && event != 22) {
                        execution.push_back(event);
                    }
                }
                const auto expected = first_turns == 3 ? std::vector<int>({1, 2, 12, 11, 9, 3})
                                                       : std::vector<int>({1, 2, 11, 12, 9, 3});
                expect(execution == expected).note("first turns = ", first_turns);
                expect_equal(std::ranges::count(log, 21), 1).note("first turns = ", first_turns);
                expect_equal(std::ranges::count(log, 22), 1).note("first turns = ", first_turns);
                const auto position = [&](int event) noexcept {
                    return std::ranges::find(log, event) - log.begin();
                };
                // Each payload closes after completion and before close delivery.
                expect(position(11) < position(21) && position(21) < position(9))
                    .note("first turns = ", first_turns);
                expect(position(12) < position(22) && position(22) < position(9))
                    .note("first turns = ", first_turns);
            }
        };
    "Runtime async: consumed cold owners retain captures through the delivery full expression"_test =
        [] static noexcept {
            for (const auto deferred : {false, true}) {
                auto alive = false;
                auto log = std::vector<int>();
                auto result = async::drive_root(captured_delivery(alive, log, deferred));
                if (!expect(result.success_if() != nullptr).note("deferred = ", deferred)) {
                    continue;
                }
                expect(!alive).note("deferred = ", deferred);
                expect(log == std::vector<int>({1, 2, 3, 4})).note("deferred = ", deferred);
                expect_equal(result.success_if()->value.value, 42);
                expect(result.success_if()->value.identity == &result.success_if()->value);
            }
        };
    "Runtime async: bindings preserve transfer and full-expression cleanup boundaries"_test =
        [] static noexcept {
            auto moves = 0;
            auto destructions = 0;
            auto binding = std::optional<async::SuccessBinding<Tracked>>();
            {
                auto source = async::drive_root(tracked(moves, destructions));
                binding.emplace(source);
                expect_equal(moves, 1);
                expect_equal(destructions, 0);
                expect_equal(binding->get().value, 17);
            }
            expect_equal(destructions, 1);
            binding.reset();
            expect_equal(destructions, 2);
            auto factories = 0;
            auto source = async::drive_root(fixed(factories));
            auto retained = async::SuccessBinding<Fixed>(source);
            expect(source.success_if() == nullptr);
            expect_equal(retained.get().value, 42);
            expect(retained.get().identity == &retained.get());
            expect_equal(factories, 1);
        };
});

} // namespace
