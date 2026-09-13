#pragma once

#include "format.hpp"

#include <cstdio>
#include <exception>
#include <string_view>
#include <type_traits>
#include <version>

#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207l
#include <print>
#endif

namespace carven::runtime {
namespace detail {

inline auto print_bytes(std::FILE* stream, std::string_view text) noexcept -> void {
    if (!text.empty() && std::fwrite(text.data(), 1, text.size(), stream) != text.size()) {
        std::terminate();
    }
}

template<typename T>
auto print_value(std::FILE* stream, const T& value) noexcept -> void {
#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207l
    std::print(stream, "{}", format_argument(value));
#else
    if constexpr (std::is_same_v<T, std::string_view>) {
        print_bytes(stream, value);
    } else if constexpr (std::is_same_v<T, String>) {
        print_bytes(stream, value.as_str());
    } else {
        print_bytes(stream, std::format("{}", format_argument(value)));
    }
#endif
}

template<bool Newline, typename First, typename... Rest>
auto print_values(std::FILE* stream, const First& first, const Rest&... rest) noexcept -> void {
    print_value(stream, first);
    ((print_bytes(stream, " "), print_value(stream, rest)), ...);
    if constexpr (Newline) {
        print_bytes(stream, "\n");
    }
}

} // namespace detail

template<typename First, typename... Rest>
auto print(const First& first, const Rest&... rest) noexcept -> void {
    detail::print_values<false>(stdout, first, rest...);
}

template<typename First, typename... Rest>
auto println(const First& first, const Rest&... rest) noexcept -> void {
    detail::print_values<true>(stdout, first, rest...);
}

inline auto println() noexcept -> void {
    println(std::string_view());
}

template<typename First, typename... Rest>
auto eprint(const First& first, const Rest&... rest) noexcept -> void {
    detail::print_values<false>(stderr, first, rest...);
}

template<typename First, typename... Rest>
auto eprintln(const First& first, const Rest&... rest) noexcept -> void {
    detail::print_values<true>(stderr, first, rest...);
}

inline auto eprintln() noexcept -> void {
    eprintln(std::string_view());
}

} // namespace carven::runtime
