#pragma once

#include <cstdint>

namespace async_benchmark {

auto iterations() noexcept -> std::uint64_t;
auto seed() noexcept -> std::uint64_t;
auto depth() noexcept -> std::uint64_t;
auto begin() noexcept -> void;
auto finish(std::uint64_t checksum) noexcept -> void;
#if CARVEN_ASYNC_BENCH_STACK
auto observe_stack() noexcept -> void;
#else
inline auto observe_stack() noexcept -> void {}
#endif

} // namespace async_benchmark

// Scalar source contracts for the generated benchmark use these native adapters.
inline auto cv_async_bench_iterations() noexcept -> std::uint64_t {
    return async_benchmark::iterations();
}

inline auto cv_async_bench_seed() noexcept -> std::uint64_t {
    return async_benchmark::seed();
}

inline auto cv_async_bench_depth() noexcept -> std::uint64_t {
    return async_benchmark::depth();
}

inline auto cv_async_bench_begin() noexcept -> void {
    async_benchmark::begin();
}

inline auto cv_async_bench_finish(std::uint64_t checksum) noexcept -> void {
    async_benchmark::finish(checksum);
}

inline auto cv_async_bench_observe_stack() noexcept -> void {
    async_benchmark::observe_stack();
}
