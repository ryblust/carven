#pragma once

#include <type_traits>
#include <utility>

namespace carven::runtime {

namespace detail {

// Value parameters must not introduce user-defined copying or destruction.
template<typename Value>
inline constexpr bool trivially_copied_and_destroyed =
    std::is_trivially_copy_constructible_v<Value> && std::is_trivially_destructible_v<Value>;

} // namespace detail

template<typename Value>
using ReadArg =
    std::conditional_t<detail::trivially_copied_and_destroyed<Value>, const Value, const Value&>;

// The caller supplies a live owner; this expression does not extend its lifetime.
template<typename Value>
    requires (!std::is_const_v<Value>)
constexpr auto transfer(Value& value) noexcept -> decltype(auto) {
    if constexpr (detail::trivially_copied_and_destroyed<Value>) {
        return std::as_const(value);
    } else {
        return std::move(value);
    }
}

// Deliver an already evaluated parameter. Reference contracts retain their exact
// category; owned value parameters use the ordinary Carven transfer policy.
template<typename Parameter>
constexpr auto deliver_argument(std::remove_reference_t<Parameter>& value) noexcept
    -> decltype(auto) {
    if constexpr (std::is_reference_v<Parameter>) {
        return std::forward<Parameter>(value);
    } else {
        return transfer(value);
    }
}

} // namespace carven::runtime
