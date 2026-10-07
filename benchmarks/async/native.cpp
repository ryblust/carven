#include "probe.hpp"

#if CARVEN_ASYNC_BENCH_LIBCORO
#include <coro/task.hpp>
#else
#include "handwritten.hpp"
#endif

#include <coroutine>
#include <cstdint>
#include <exception>

namespace {

#if CARVEN_ASYNC_BENCH_LIBCORO
using Task = coro::task<std::uint64_t>;
#else
using Task = handwritten::Task;
#endif

struct Yield final {
    std::coroutine_handle<> handle;
    Yield* next;
    auto await_ready() const noexcept -> bool;
    auto await_suspend(std::coroutine_handle<> continuation) noexcept -> void;
    auto await_resume() const noexcept -> void;
};

class Queue final {
public:
    auto push(Yield& yield) noexcept -> void;
    auto resume_one() noexcept -> void;

private:
    Yield* head = nullptr;
    Yield* tail = nullptr;
};

auto Queue::push(Yield& yield) noexcept -> void {
    yield.next = nullptr;
    if (tail != nullptr) {
        tail->next = &yield;
    } else {
        head = &yield;
    }
    tail = &yield;
}

auto Queue::resume_one() noexcept -> void {
    if (head == nullptr) {
        std::terminate();
    }
    auto* node = head;
    head = node->next;
    if (head == nullptr) {
        tail = nullptr;
    }
    const auto handle = node->handle;
    handle.resume(); // Do not access the awaiter after resume.
}

Queue queue;

auto Yield::await_ready() const noexcept -> bool {
    return false;
}

auto Yield::await_resume() const noexcept -> void {}

auto Yield::await_suspend(std::coroutine_handle<> continuation) noexcept -> void {
    handle = continuation;
    queue.push(*this);
}

[[maybe_unused]] auto leaf(std::uint64_t value, bool deferred) noexcept -> Task {
    async_probe::observe_stack();
    if (deferred) {
        co_await Yield {.handle = {}, .next = nullptr};
    }
    co_return value + 1;
}

[[maybe_unused]] auto ready_leaf(std::uint64_t value) noexcept -> Task {
    async_probe::observe_stack();
    co_return value + 1;
}

[[maybe_unused]] auto yielding_leaf(std::uint64_t value) noexcept -> Task {
    async_probe::observe_stack();
    co_await Yield {.handle = {}, .next = nullptr};
    co_return value + 1;
}

template<bool Deferred>
[[maybe_unused]] auto static_leaf(std::uint64_t value) noexcept -> Task {
    if constexpr (Deferred) {
        return yielding_leaf(value);
    } else {
        return ready_leaf(value);
    }
}

[[maybe_unused]] auto chain(std::uint64_t value, std::uint64_t depth) noexcept -> Task {
    async_probe::observe_stack();
    if (depth == 0) {
        co_return value;
    }
    co_return co_await chain(value + 1, depth - 1);
}

auto run_workload() noexcept -> Task {
    const auto iterations = async_probe::iterations();
    const auto seed = async_probe::seed();
    [[maybe_unused]] const auto depth = async_probe::depth();
    auto checksum = 0ull;
    async_probe::begin();
    for (auto index = 0ull; index < iterations; ++index) {
        const auto value = (seed + index) % 1048576;
#if CARVEN_ASYNC_BENCH_CHAIN
        checksum += co_await chain(value, depth);
#elif CARVEN_ASYNC_BENCH_STATIC
        checksum += co_await static_leaf<CARVEN_ASYNC_BENCH_YIELD != 0>(value);
#elif CARVEN_ASYNC_BENCH_STORED
        auto operation = leaf(value, false);
        const auto following = value + 1;
        checksum += (co_await operation) + following;
#else
        checksum += co_await leaf(value, CARVEN_ASYNC_BENCH_YIELD != 0);
#endif
    }
    async_probe::finish(checksum);
    co_return checksum;
}

} // namespace

auto main() noexcept -> int {
    auto task = run_workload();
#if CARVEN_ASYNC_BENCH_LIBCORO
    task.resume();
    while (!task.is_ready()) {
#else
    task.start();
    while (!task.ready()) {
#endif
        queue.resume_one();
    }

    return 0;
}
