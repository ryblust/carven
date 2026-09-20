module;
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

module carven:support.terminal.impl;

import :support.terminal;
import std;

namespace {

constexpr auto ansi_reset = "\033[0m";
constexpr auto ansi_bold = "\033[1m";
constexpr auto ansi_bold_red = "\033[1;31m";
constexpr auto ansi_bold_green = "\033[1;32m";
constexpr auto ansi_bold_yellow = "\033[1;33m";
constexpr auto ansi_bold_cyan = "\033[1;36m";

auto wrap(bool enabled, std::string_view prefix, std::string_view text) noexcept -> std::string {
    if (!enabled || text.empty()) {
        return std::string(text);
    }
    return std::format("{}{}{}", prefix, text, ansi_reset);
}

} // namespace

auto enable_stderr_ansi() noexcept -> bool {
#ifdef _WIN32
    const auto handle = GetStdHandle(STD_ERROR_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return false;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(handle, &mode)) {
        return false;
    }
    if ((mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
        if (!SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
            return false;
        }
    }
    return true;
#else
    return ::isatty(STDERR_FILENO) != 0;
#endif
}

TerminalStyler::TerminalStyler(bool use_color) noexcept
    : enabled(use_color) {}

auto TerminalStyler::bold(std::string_view text) const noexcept -> std::string {
    return wrap(enabled, ansi_bold, text);
}

auto TerminalStyler::bold_red(std::string_view text) const noexcept -> std::string {
    return wrap(enabled, ansi_bold_red, text);
}

auto TerminalStyler::bold_green(std::string_view text) const noexcept -> std::string {
    return wrap(enabled, ansi_bold_green, text);
}

auto TerminalStyler::bold_yellow(std::string_view text) const noexcept -> std::string {
    return wrap(enabled, ansi_bold_yellow, text);
}

auto TerminalStyler::bold_cyan(std::string_view text) const noexcept -> std::string {
    return wrap(enabled, ansi_bold_cyan, text);
}
