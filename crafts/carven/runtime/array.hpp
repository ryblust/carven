#pragma once

#include "numeric.hpp"

#include <array>
#include <cstddef>
#include <cstdlib>
#include <type_traits>

namespace carven::runtime {

namespace detail {

template<std::size_t Extent, Integer Index>
constexpr auto checked_array_offset(Index index) noexcept -> std::size_t {
    if constexpr (std::signed_integral<Index>) {
        if (index < 0) {
            std::abort();
        }
    }
    if (static_cast<std::make_unsigned_t<Index>>(index) >= Extent) {
        std::abort();
    }
    return static_cast<std::size_t>(index);
}

} // namespace detail

template<typename Element, std::size_t Extent, Integer Index>
constexpr auto checked_array_index(std::array<Element, Extent>& array, Index index) noexcept
    -> Element& {
    return array[detail::checked_array_offset<Extent>(index)];
}

template<typename Element, std::size_t Extent, Integer Index>
constexpr auto checked_array_index(const std::array<Element, Extent>& array, Index index) noexcept
    -> const Element& {
    return array[detail::checked_array_offset<Extent>(index)];
}

} // namespace carven::runtime
