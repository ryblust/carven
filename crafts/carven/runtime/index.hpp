#pragma once

#include "numeric.hpp"

#include <cinttypes>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>

namespace carven::runtime {

namespace detail {

template<Integer Index>
[[noreturn]] CARVEN_RUNTIME_COLD auto index_trap(
    Index index,
    std::size_t extent,
    SourceSite site
) noexcept -> void {
    begin_report("sequence index is out of bounds", site);
    if constexpr (std::signed_integral<Index>) {
        std::fprintf(stderr, "  index: %" PRIdMAX "\n", static_cast<std::intmax_t>(index));
    } else {
        std::fprintf(stderr, "  index: %" PRIuMAX "\n", static_cast<std::uintmax_t>(index));
    }
    std::fprintf(stderr, "  length: %zu\n", extent);
    abort_report();
}

} // namespace detail

template<Integer Index>
constexpr auto checked_index_offset(Index index, std::size_t extent, SourceSite site) noexcept
    -> std::size_t {
    if constexpr (std::signed_integral<Index>) {
        if (index < 0) {
            detail::index_trap(index, extent, site);
        }
    }
    if (static_cast<std::make_unsigned_t<Index>>(index) >= extent) {
        detail::index_trap(index, extent, site);
    }
    return static_cast<std::size_t>(index);
}

} // namespace carven::runtime
