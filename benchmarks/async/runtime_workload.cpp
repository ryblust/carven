#include "measurement.hpp"

#include <carven/runtime/execution/execution.hpp>
#include <cstdint>
#include <utility>

namespace async_benchmark {
namespace async = carven::runtime::execution;

#if CARVEN_ASYNC_BENCH_STORED
// A saved cold operation keeps its activation across the statement boundary.
auto leaf(std::uint64_t value) noexcept -> async::Operation<std::uint64_t> {
    async_benchmark::observe_stack();
    co_return async::Completion<std::uint64_t>::success(value + 1);
}
#endif

// Match the compiler's embedded scalar leaf and tail-loop orchestration while
// retaining the actual task context, completion carrier, and FIFO driver.
auto root() noexcept -> async::Operation<void> {
    const auto count = async_benchmark::iterations();
    const auto initial = async_benchmark::seed();
    [[maybe_unused]] const auto nesting = async_benchmark::depth();
    std::uint64_t checksum = 0;
    async_benchmark::begin();
    for (std::uint64_t index = 0; index < count; ++index) {
        const auto value = (initial + index) % std::uint64_t {1048576};
#if CARVEN_ASYNC_BENCH_STORED
        auto operation = leaf(value);
        const auto following = value + 1;
        const auto result = co_await std::move(operation);
        checksum += result.success_if()->value + following;
#elif CARVEN_ASYNC_BENCH_CHAIN
        auto result = value;
        for (auto remaining = nesting;; --remaining) {
            async_benchmark::observe_stack();
            if (remaining == 0) {
                break;
            }
            ++result;
        }
        checksum += result;
#else
        const auto before = checksum;
        async_benchmark::observe_stack();
#if CARVEN_ASYNC_BENCH_YIELD
        co_await async::await_yield_once();
#endif
        checksum = before + value + 1;
#endif
    }
    async_benchmark::finish(checksum);
    co_return async::Completion<void>::success();
}

} // namespace async_benchmark

auto main() noexcept -> int {
    carven::runtime::execution::drive_root(async_benchmark::root());
    return 0;
}
