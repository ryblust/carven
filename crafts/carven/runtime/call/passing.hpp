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

// A native result query may denote borrowed storage or a fresh value. Saving
// a source Read argument snapshots the former when ReadArg selects a value,
// while the latter always keeps its own backing alive.
template<typename Source>
using ReadArgumentStorage = std::conditional_t<
    std::is_reference_v<Source>,
    ReadArg<std::remove_cvref_t<Source>>,
    std::remove_cvref_t<Source>>;

// Only standard pointer conversions preserve an existing storage identity.
template<typename Parameter, typename Source>
inline constexpr bool read_storage_compatible = std::
    is_convertible_v<const std::remove_reference_t<Source>*, const std::remove_cvref_t<Parameter>*>;

template<typename Parameter, typename Source>
using ReadConversionStorage = std::conditional_t<
    std::is_reference_v<Parameter> && read_storage_compatible<Parameter, Source>,
    Parameter,
    std::remove_cvref_t<Parameter>>;

// The caller retains complete backing. OriginalQuery records its category before
// anchoring; an ordinary named Read source supplies a const lvalue query.
template<typename Parameter, typename OriginalQuery, typename Source>
constexpr auto read_conversion(Source& source) noexcept
    -> ReadConversionStorage<Parameter, Source> {
    using Storage = ReadConversionStorage<Parameter, Source>;
    if constexpr (read_storage_compatible<Parameter, Source>
                  || std::is_lvalue_reference_v<OriginalQuery>) {
        return static_cast<Storage>(std::as_const(source));
    } else {
        return static_cast<Storage>(std::forward<OriginalQuery>(source));
    }
}

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
