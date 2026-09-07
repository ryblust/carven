#pragma once

#include <cstdint>
#include <iostream>
#include <string_view>

namespace example {

inline auto print(std::string_view message) noexcept -> void {
    std::cout << message << '\n';
}

inline auto print(std::int32_t value) noexcept -> void {
    std::cout << value << '\n';
}

}
