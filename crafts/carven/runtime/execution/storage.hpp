#pragma once

#include <array>
#include <cstddef>
#include <type_traits>

namespace carven::runtime {

template<typename... Elements>
struct Tuple;

namespace execution::detail {

// Generated declarations publish source value semantics before their definitions.
// Native class representations remain independently stored unless explicitly known.
template<typename Value>
struct InlineValue : std::bool_constant<[] {
    if constexpr (requires { carven_inline_value(static_cast<Value*>(nullptr)); }) {
        return decltype(carven_inline_value(static_cast<Value*>(nullptr)))::value;
    } else {
        return std::is_void_v<Value>
            || std::is_arithmetic_v<Value>
            || std::is_pointer_v<Value>
            || std::is_enum_v<Value>;
    }
}()> {};

template<typename Element, std::size_t Size>
struct InlineValue<std::array<Element, Size>> : InlineValue<Element> {};

template<typename... Elements>
struct InlineValue<Tuple<Elements...>> : std::bool_constant<(InlineValue<Elements>::value && ...)> {
};

} // namespace execution::detail

} // namespace carven::runtime
