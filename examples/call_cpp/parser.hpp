#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// Native interface: malformed input and invalid ports throw C++ exceptions.
inline auto parse_port(std::string_view text) -> std::int32_t {
    const auto input = std::string(text);
    auto consumed = std::size_t {0};
    const auto value = std::stoi(input, &consumed);
    if (consumed != input.size() || value < 1 || value > 65535) {
        throw std::invalid_argument("expected a port between 1 and 65535");
    }
    return value;
}
