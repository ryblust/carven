#pragma once

#include <carven/runtime/value/void.hpp>

#include <cstddef>
#include <utility>

namespace carven::runtime {

template<typename... Elements>
struct Tuple;

template<>
struct Tuple<> final {
    auto operator==(const Tuple&) const noexcept -> bool = default;
};

// Nested aggregate initialization constructs each element directly in its final
// storage, including elements with neither copy nor move constructors.
template<typename Head, typename... Tail>
struct Tuple<Head, Tail...> final {
    [[no_unique_address]] Head head;
    [[no_unique_address]] Tuple<Tail...> tail;

    auto operator==(const Tuple&) const noexcept -> bool = default;
};

template<std::size_t Index, typename Head, typename... Tail>
constexpr auto tuple_get(Tuple<Head, Tail...>& value) noexcept -> decltype(auto) {
    static_assert(Index < 1 + sizeof...(Tail));
    if constexpr (Index == 0) {
        return (value.head);
    } else {
        return tuple_get<Index - 1>(value.tail);
    }
}

template<std::size_t Index, typename Head, typename... Tail>
constexpr auto tuple_get(const Tuple<Head, Tail...>& value) noexcept -> decltype(auto) {
    static_assert(Index < 1 + sizeof...(Tail));
    if constexpr (Index == 0) {
        return (value.head);
    } else {
        return tuple_get<Index - 1>(value.tail);
    }
}

template<std::size_t Index, typename Head, typename... Tail>
constexpr auto tuple_get(Tuple<Head, Tail...>&& value) noexcept -> decltype(auto) {
    return std::move(tuple_get<Index>(value));
}

template<std::size_t Index, typename Head, typename... Tail>
constexpr auto tuple_get(const Tuple<Head, Tail...>&& value) noexcept -> decltype(auto) {
    return std::move(tuple_get<Index>(value));
}

} // namespace carven::runtime
