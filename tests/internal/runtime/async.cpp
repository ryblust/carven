module;
#include <carven/runtime/async/async.hpp>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

module carven:test.internal.runtime.async;

import :test.harness.framework;

namespace {
namespace async = carven::runtime::async;

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
static_assert(!std::is_move_assignable_v<async::Operation<int>>);
static_assert(!std::is_move_constructible_v<async::Child<int>>);

auto deep(int depth, int& bodies) noexcept -> async::Operation<int> {
    ++bodies;
    if (depth == 0) {
        co_return async::Completion<int>::success(0);
    }
    auto next = co_await deep(depth - 1, bodies);
    co_return async::Completion<int>::success(next.success_if()->value + 1);
}

auto deep_close(int depth, int& bodies) noexcept -> async::Operation<void> {
    ++bodies;
    if (depth != 0) {
        auto scope = async::ChildScope(co_await async::current_activation());
        auto child = scope.start(deep_close(depth - 1, bodies));
        co_await scope.close(async::ClosingPolicy::RequestCancelAndClose);
    }
    co_return async::Completion<void>::success();
}

auto fixed(int& factories) noexcept -> async::Operation<Fixed> {
    co_return async::Completion<Fixed>::success_from([&]() noexcept {
        ++factories;
        return Fixed(42);
    });
}

auto numbered(std::vector<int>& log, int number) noexcept -> async::Operation<void> {
    log.push_back(number);
    auto yielded = co_await async::yield_once();
    if (yielded.success_if() == nullptr) {
        co_return async::Completion<void>::cancelled();
    }
    log.push_back(number + 10);
    co_return async::Completion<void>::success();
}

auto fifo(std::vector<int>& log) noexcept -> async::Operation<void> {
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

auto nested_checkpoint(bool& same_task, async::Operation<void> checkpoint) noexcept
    -> async::Operation<void> {
    same_task = async::cancellation_requested();
    co_return co_await async::Operation<void>(std::move(checkpoint));
}

auto accepting(bool& yield_succeeded, bool& same_task, async::Operation<void> checkpoint) noexcept
    -> async::Operation<void> {
    auto turn = async::yield_once();
    auto yielded = co_await async::Operation<void>(std::move(turn));
    yield_succeeded = yielded.success_if() != nullptr && async::cancellation_requested();
    co_return co_await nested_checkpoint(same_task, std::move(checkpoint));
}

auto cancel_child(bool& yield_succeeded, bool& same_task, bool& parent_unrequested) noexcept
    -> async::Operation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto checkpoint = async::cancellation_point();
    auto child = scope.start(accepting(yield_succeeded, same_task, std::move(checkpoint)));
    async::cancel(child);
    async::cancel(child);
    parent_unrequested = !async::cancellation_requested();
    auto completion = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return completion;
}

auto sibling(bool& released, int& finished) noexcept -> async::Operation<void> {
    while (!released) {
        auto result = co_await async::yield_once();
        if (result.success_if() == nullptr) {
            co_return async::Completion<void>::cancelled();
        }
    }
    ++finished;
    co_return async::Completion<void>::success();
}

auto immediate() noexcept -> async::Operation<int> {
    co_return async::Completion<int>::success(7);
}

auto local_child_scope() noexcept -> async::Operation<int> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(immediate());
    auto result = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return result;
}

auto separate_owners(int& finished) noexcept -> async::Operation<void> {
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

auto grandchild(bool& alive, std::vector<int>& log) noexcept -> async::Operation<Abandoned> {
    auto yielded = co_await async::yield_once();
    log.push_back(yielded.success_if() != nullptr && alive ? 1 : -1);
    co_return async::Completion<Abandoned>::success_from([&]() noexcept {
        return Abandoned(alive, log);
    });
}

auto closing_parent(bool& alive, std::vector<int>& log) noexcept -> async::Operation<void> {
    auto backing = Backing(alive, log);
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(grandchild(alive, log));
    auto checkpoint = co_await async::cancellation_point();
    // Cancellation selects an outcome; lexical closure still drains the child.
    co_await scope.close(async::ClosingPolicy::RequestCancelAndClose);
    co_return checkpoint;
}

auto nested_close(bool& alive, std::vector<int>& log) noexcept -> async::Operation<void> {
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(closing_parent(alive, log));
    async::cancel(child);
    auto completion = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return completion;
}

auto unobserved_sibling(bool& alive, std::vector<int>& log, int number, int turns) noexcept
    -> async::Operation<Abandoned> {
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
    -> async::Operation<void> {
    auto backing = Backing(alive, log);
    auto scope = async::ChildScope(co_await async::current_activation());
    auto first = scope.start(unobserved_sibling(alive, log, 1, first_turns));
    auto second = scope.start(unobserved_sibling(alive, log, 2, second_turns));
    co_await scope.close(async::ClosingPolicy::RequestCancelAndClose);
    log.push_back(alive ? 9 : -9);
    co_return async::Completion<void>::success();
}

auto cancelled_before_close(std::vector<int>& log) noexcept -> async::Operation<void> {
    auto result = co_await async::cancellation_point();
    log.push_back(result.is_cancelled() ? 1 : -1);
    co_return result;
}

auto mark(std::vector<int>& log) noexcept -> async::Operation<void> {
    log.push_back(7);
    co_return async::Completion<void>::success();
}

auto close_once(std::vector<int>& log) noexcept -> async::Operation<void> {
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

auto record(std::vector<int>& log, int number) noexcept -> async::Operation<void> {
    log.push_back(number);
    co_return async::Completion<void>::success();
}

auto yielding_dependency(std::vector<int>& log) noexcept -> async::Operation<void> {
    log.push_back(1);
    auto ready = co_await record(log, 2);
    auto turn = async::yield_once();
    log.push_back(3);
    auto yielded = co_await async::Operation<void>(std::move(turn));
    log.push_back(4);
    co_return ready.success_if() != nullptr&& yielded.success_if() != nullptr
        ? async::Completion<void>::success()
        : async::Completion<void>::cancelled();
}

auto explicit_boundary(std::vector<int>& log) noexcept -> async::Operation<void> {
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

auto inherited_checkpoint(bool& inherited) noexcept -> async::Operation<void> {
    inherited = async::cancellation_requested();
    co_return co_await async::cancellation_point();
}

auto cancelled_context(bool& before, bool& after, bool& inherited) noexcept
    -> async::Operation<void> {
    before = async::cancellation_requested();
    auto yielded = co_await async::yield_once();
    after = yielded.success_if() != nullptr && async::cancellation_requested();
    auto scope = async::ChildScope(co_await async::current_activation());
    auto child = scope.start(inherited_checkpoint(inherited));
    auto result = co_await child.observe();
    co_await scope.close(async::ClosingPolicy::CloseOnly);
    co_return result;
}

auto unrequested_context(bool& before, bool& after) noexcept -> async::Operation<void> {
    before = !async::cancellation_requested();
    auto yielded = co_await async::yield_once();
    after = yielded.success_if() != nullptr && !async::cancellation_requested();
    co_return async::Completion<void>::success();
}

auto sibling_contexts(bool& restored, bool& inherited) noexcept -> async::Operation<void> {
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
        && !async::cancellation_requested();
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
) noexcept -> async::Operation<Fixed> {
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
    -> async::Operation<Fixed> {
    auto cold = captured_result(Capture(alive, log), log, deferred);
    auto moved = async::Operation<Fixed>(std::move(cold));
    if (!alive || !log.empty()) {
        co_return async::Completion<Fixed>::cancelled();
    }
    auto result =
        inspect_completion(co_await async::Operation<Fixed>(std::move(moved)), alive, log);
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

auto tracked(int& moves, int& destructions) noexcept -> async::Operation<Tracked> {
    co_return async::Completion<Tracked>::success_from([&]() noexcept {
        return Tracked(moves, destructions);
    });
}

const TestSuite suite([] static noexcept {
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
            expect(!async::cancellation_requested());
            auto result = async::drive_root(cancel_child(yielded, same_task, parent_unrequested));
            expect(result.is_cancelled());
            expect(yielded && same_task && parent_unrequested);
            expect(!async::cancellation_requested());
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
            expect(!async::cancellation_requested());
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
