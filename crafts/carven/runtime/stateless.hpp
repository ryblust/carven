#pragma once

#include <concepts>
#include <type_traits>

namespace carven::runtime {

// Type-selected operations share a const instance. Invocation requirements
// belong to the consuming interface.
template<typename T>
concept Stateless = std::same_as<T, std::remove_cvref_t<T>>
    && std::is_object_v<T>
    && std::is_empty_v<T>
    && std::is_trivially_default_constructible_v<T>
    && std::is_trivially_destructible_v<T>;

template<Stateless T>
inline constexpr T stateless_value {};

} // namespace carven::runtime
