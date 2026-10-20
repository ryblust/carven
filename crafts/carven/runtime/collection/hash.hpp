#pragma once

#include <carven/runtime/value/tuple.hpp>

#include <array>
#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

namespace carven::runtime {

inline constexpr auto hash_seed = std::uint64_t(14695981039346656037ull);

constexpr auto hash_byte(std::uint64_t state, std::uint8_t byte) noexcept -> std::uint64_t {
    return (state ^ byte) * 1099511628211ull;
}

constexpr auto hash_word(std::uint64_t state, std::uint64_t word) noexcept -> std::uint64_t {
    for (auto index = 0u; index < 8; ++index) {
        state = hash_byte(state, static_cast<std::uint8_t>(word));
        word >>= 8;
    }
    return state;
}

constexpr auto hash_bytes(std::string_view bytes) noexcept -> std::uint64_t {
    auto state = hash_seed;
    for (const auto byte : bytes) {
        state = hash_byte(state, static_cast<std::uint8_t>(byte));
    }
    return state;
}

struct ValueHash final {
    template<typename T>
        requires std::is_integral_v<T>
    constexpr auto operator()(T value) const noexcept -> std::uint64_t {
        return hash_word(hash_seed, static_cast<std::uint64_t>(value));
    }

    constexpr auto operator()(std::string_view value) const noexcept -> std::uint64_t {
        return hash_bytes(value);
    }

    template<typename T>
        requires requires (const T& value) { value.as_str(); }
    auto operator()(const T& value) const noexcept -> std::uint64_t {
        return hash_bytes(value.as_str());
    }

    template<typename T, std::size_t N>
    constexpr auto operator()(const std::array<T, N>& value) const noexcept -> std::uint64_t {
        auto state = hash_seed;
        for (const auto& element : value) {
            state = hash_word(state, (*this)(element));
        }
        return state;
    }

    template<typename... Elements>
    constexpr auto operator()(const Tuple<Elements...>& value) const noexcept -> std::uint64_t {
        auto state = hash_seed;
        [&]<std::size_t... I>(std::index_sequence<I...>) noexcept {
            ((state = hash_word(state, (*this)(tuple_get<I>(value)))), ...);
        }(std::index_sequence_for<Elements...> {});
        return state;
    }
};

inline constexpr auto hash_value = ValueHash {};

} // namespace carven::runtime
