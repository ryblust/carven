#pragma once

#include "index.hpp"

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace carven::runtime {

namespace detail {

template<typename Destination, bool Stateless, typename Source>
constexpr auto adopt_array_value(const Source& source) noexcept -> Destination;

template<typename Destination, bool Stateless, typename Source, std::size_t N>
constexpr auto adopt_array_value(const std::array<Source, N>& source) noexcept -> Destination;

template<typename Destination, bool Stateless, typename Source, std::size_t N, std::size_t... I>
constexpr auto adopt_array_elements(
    const std::array<Source, N>& source,
    std::index_sequence<I...>
) noexcept -> Destination {
    return {adopt_array_value<typename Destination::value_type, Stateless>(source[I])...};
}

template<typename Destination, bool Stateless, typename Source>
constexpr auto adopt_array_value(const Source& source) noexcept -> Destination {
    if constexpr (std::is_same_v<Destination, Source>) {
        return source;
    } else if constexpr (Stateless) {
        return Destination::from_stateless(source);
    } else {
        return Destination {source};
    }
}

template<typename Destination, bool Stateless, typename Source, std::size_t N>
constexpr auto adopt_array_value(const std::array<Source, N>& source) noexcept -> Destination {
    static_assert(std::tuple_size_v<Destination> == N);
    if constexpr (std::is_same_v<Destination, std::array<Source, N>>) {
        return source;
    } else {
        return adopt_array_elements<Destination, Stateless>(source, std::make_index_sequence<N> {});
    }
}

} // namespace detail

template<typename Element, std::size_t Extent, Integer Index>
constexpr auto checked_array_index(std::array<Element, Extent>& array, Index index) noexcept
    -> Element& {
    return array[checked_index_offset(index, Extent)];
}

template<typename Element, std::size_t Extent, Integer Index>
constexpr auto checked_array_index(const std::array<Element, Extent>& array, Index index) noexcept
    -> const Element& {
    return array[checked_index_offset(index, Extent)];
}

// The caller retains source storage for borrowed elements. Stateless selects
// the source-language callable policy; native types determine array structure.
template<typename Destination, bool Stateless, typename Source, std::size_t Extent>
constexpr auto adopt_array(const std::array<Source, Extent>& source) noexcept -> Destination {
    return detail::adopt_array_value<Destination, Stateless>(source);
}

} // namespace carven::runtime
