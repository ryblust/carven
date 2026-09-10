#pragma once

#include "text.hpp"

#include <cstddef>
#include <ranges>
#include <string_view>
#include <utility>

namespace carven::runtime {

constexpr auto entry_args(int argc, const char* const* argv) noexcept -> auto {
    const auto count = argc > 1 ? static_cast<std::size_t>(argc - 1) : std::size_t {0};
    for (auto index = std::size_t {0}; index < count; ++index) {
        checked_utf8(std::string_view(argv[index + 1]));
    }
    return std::views::iota(std::size_t {0}, count)
        | std::views::transform([argv](std::size_t index) noexcept {
               return std::pair {index, std::string_view(argv[index + 1])};
           });
}

} // namespace carven::runtime
