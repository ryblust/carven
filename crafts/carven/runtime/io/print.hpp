#pragma once

#include <cstdio>
#include <exception>
#include <string_view>

namespace carven::runtime {

namespace detail {

inline auto print_bytes(std::FILE* stream, std::string_view text) noexcept -> void {
    if (!text.empty() && std::fwrite(text.data(), 1, text.size(), stream) != text.size()) {
        std::terminate();
    }
}

template<bool Newline, typename First, typename... Rest>
auto print_values(std::FILE* stream, const First& first, const Rest&... rest) noexcept -> void {
    detail::print_bytes(stream, first);
    ((detail::print_bytes(stream, " "), detail::print_bytes(stream, rest)), ...);
    if constexpr (Newline) {
        detail::print_bytes(stream, "\n");
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
