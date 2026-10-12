#pragma once

#include <carven/runtime/collection/slice.hpp>

#include <cstddef>
#include <cstdint>

namespace simd_benchmark {

using Bytes = carven::runtime::Slice<std::uint8_t>;
auto scan(Bytes bytes, std::size_t start) noexcept -> std::size_t;

} // namespace simd_benchmark
