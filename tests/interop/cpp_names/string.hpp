#pragma once
#include <carven/runtime/string.hpp>
#include <carven/runtime/passing.hpp>
#include <string_view>

namespace native_text {
inline std::string_view retained;

inline auto read(std::string_view value) noexcept -> std::size_t {
    return value.size();
}

inline auto remember(std::string_view value) noexcept -> void {
    retained = value;
}

inline auto retained_size() noexcept -> std::size_t {
    return retained.size();
}

inline auto forget() noexcept -> void {
    retained = {};
}

inline auto external_view() noexcept -> std::string_view {
    return "native";
}

inline auto replace(std::string_view& value) noexcept -> void {
    value = "native";
}

template<typename T>
auto read_aggregate(const T& value) noexcept -> std::size_t {
    return value.text.size();
}

inline auto aliases_retained(carven::runtime::ReadArg<carven::runtime::String> value) noexcept
    -> bool {
    return value.as_str().data() == retained.data();
}
} // namespace native_text

struct String final {
    int value;
};
