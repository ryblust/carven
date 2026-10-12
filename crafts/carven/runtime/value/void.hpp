#pragma once

#include <type_traits>
#include <utility>

namespace carven::runtime {

// Object storage for the source language's zero-information value. Callable
// completion ABIs continue to use native void.
struct Void final {
    auto operator==(const Void&) const noexcept -> bool = default;
};

template<typename Result>
using NativeValue = std::conditional_t<std::is_void_v<Result>, Void, Result>;

// Native result types may remain delegated until C++ compilation. Execute the
// selected expression once, preserving references and direct value construction.
template<typename Factory>
constexpr auto materialize_native(Factory&& factory) noexcept
    -> NativeValue<std::invoke_result_t<Factory&&>> {
    if constexpr (std::is_void_v<std::invoke_result_t<Factory&&>>) {
        std::forward<Factory>(factory)();
        return {};
    } else {
        return std::forward<Factory>(factory)();
    }
}

} // namespace carven::runtime
