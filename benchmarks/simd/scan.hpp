#pragma once

#include <carven/runtime/collection/slice.hpp>

#include <cstddef>
#include <cstdint>

namespace simd_benchmark {

using Bytes = carven::runtime::Slice<std::uint8_t>;
#if CARVEN_SIMD_BENCH_COUNT
auto scan(Bytes bytes) noexcept -> std::size_t;
#else
auto scan(Bytes bytes, std::size_t start) noexcept -> std::size_t;
#endif

} // namespace simd_benchmark
