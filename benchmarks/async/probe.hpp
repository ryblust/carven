#pragma once

#include <cstdint>

namespace async_probe {

auto iterations() noexcept -> std::uint64_t;
auto seed() noexcept -> std::uint64_t;
auto depth() noexcept -> std::uint64_t;
auto begin() noexcept -> void;
auto finish(std::uint64_t checksum) noexcept -> void;
auto observe_stack() noexcept -> void;

} // namespace async_probe

// Scalar source contracts for the generated benchmark use these native adapters.
inline auto cv_async_bench_iterations() noexcept -> std::uint64_t {
    return async_probe::iterations();
}

inline auto cv_async_bench_seed() noexcept -> std::uint64_t {
    return async_probe::seed();
}

inline auto cv_async_bench_depth() noexcept -> std::uint64_t {
    return async_probe::depth();
}

inline auto cv_async_bench_begin() noexcept -> void {
    async_probe::begin();
}

inline auto cv_async_bench_finish(std::uint64_t checksum) noexcept -> void {
    async_probe::finish(checksum);
}

inline auto cv_async_bench_observe_stack() noexcept -> void {
    async_probe::observe_stack();
}
