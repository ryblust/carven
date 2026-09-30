#pragma once

#include <csignal>
#include <cstdlib>
#include <string_view>

inline auto install_abort_handler() noexcept -> void {
    std::signal(SIGABRT, +[](int signal) noexcept { std::_Exit(signal == SIGABRT ? 86 : 87); });
}

inline auto selected(const auto& arguments, std::string_view expected) noexcept -> bool {
    for (const auto& [index, argument] : arguments) {
        if (index == 0 && argument == expected) {
            return true;
        }
    }
    return false;
}
