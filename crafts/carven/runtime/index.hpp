#pragma once

#include "numeric.hpp"

#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <type_traits>

namespace carven::runtime {

template<Integer Index>
constexpr auto checked_index_offset(Index index, std::size_t extent) noexcept -> std::size_t {
    if constexpr (std::signed_integral<Index>) {
        if (index < 0) {
            std::abort();
        }
    }
    if (static_cast<std::make_unsigned_t<Index>>(index) >= extent) {
        std::abort();
    }
    return static_cast<std::size_t>(index);
}

} // namespace carven::runtime
