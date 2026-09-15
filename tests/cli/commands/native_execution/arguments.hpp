#pragma once

#include <iostream>

inline auto show_args(const auto& arguments) noexcept -> void {
    for (const auto& argument : arguments) {
        std::cout << argument.second << '\n';
    }
}
