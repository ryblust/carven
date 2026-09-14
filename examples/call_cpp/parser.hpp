#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// Valid ports are positive, so -1 is an application-defined error result.
inline auto parse_port(std::string_view text) -> std::int32_t {
    try {
        const auto input = std::string(text);
        auto consumed = std::size_t {0};
        const auto value = std::stoi(input, &consumed);
        if (consumed != input.size() || value < 1 || value > 65535) {
            return -1;
        }
        return value;
    } catch (const std::invalid_argument&) {
        return -1;
    } catch (const std::out_of_range&) {
        return -1;
    }
}
