#include "probe.hpp"

#include <carven/runtime/async/async.hpp>
#include <cstdint>
#include <utility>

namespace handwritten_runtime {
namespace async = carven::runtime::async;

#if CARVEN_ASYNC_BENCH_STORED
// A saved cold operation keeps its activation across the statement boundary.
auto leaf(std::uint64_t value) noexcept -> async::Operation<std::uint64_t> {
    async_probe::observe_stack();
    co_return async::Completion<std::uint64_t>::success(value + 1);
}
#endif

// Match the compiler's embedded scalar leaf and tail-loop orchestration while
// retaining the actual task context, completion carrier, and FIFO driver.
auto root() noexcept -> async::Operation<void> {
    const auto count = async_probe::iterations();
    const auto initial = async_probe::seed();
    [[maybe_unused]] const auto nesting = async_probe::depth();
    std::uint64_t checksum = 0;
    async_probe::begin();
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
            async_probe::observe_stack();
            if (remaining == 0) {
                break;
            }
            ++result;
        }
        checksum += result;
#else
        const auto before = checksum;
        async_probe::observe_stack();
#if CARVEN_ASYNC_BENCH_YIELD
        co_await async::await_yield_once();
#endif
        checksum = before + value + 1;
#endif
    }
    async_probe::finish(checksum);
    co_return async::Completion<void>::success();
}
} // namespace handwritten_runtime

auto main() noexcept -> int {
    carven::runtime::async::drive_root(handwritten_runtime::root());
    return 0;
}
