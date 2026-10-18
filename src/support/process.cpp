module;

#if defined(_WIN32)
#include <cstdlib>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <corecrt_startup.h>
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif

module carven:support.process.impl;

import :support.process;
import :support.utf8;
import std;

#if defined(_WIN32)
namespace {

auto windows_text_to_utf8(const wchar_t* text) noexcept
    -> std::expected<std::string, std::string_view> {
    const auto length =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (length == 0) {
        return std::unexpected("host text is not valid UTF-16");
    }
    auto result = std::string(static_cast<std::size_t>(length), '\0');
    if (WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            text,
            -1,
            result.data(),
            length,
            nullptr,
            nullptr
        )
        != length) {
        return std::unexpected("cannot encode host text as UTF-8");
    }
    result.pop_back();
    return result;
}

} // namespace
#endif

namespace process {

auto arguments(int argc, [[maybe_unused]] const char* const* argv) noexcept
    -> std::expected<std::vector<std::string>, std::string_view> {
#if defined(_WIN32)
    // Narrow argv may have lost characters through the system code page.
    // The CRT preserves quoting and empty arguments in its wide argument vector.
    if (_configure_wide_argv(_crt_argv_unexpanded_arguments) != 0) {
        return std::unexpected("cannot read process arguments");
    }
    argc = __argc;
#endif
    auto result = std::vector<std::string>();
    result.reserve(static_cast<std::size_t>(argc));
    for (auto index = 0; index < argc; ++index) {
#if defined(_WIN32)
        auto text = windows_text_to_utf8(__wargv[index]);
        if (!text) {
            return std::unexpected(text.error());
        }
        result.push_back(std::move(*text));
#else
        const auto text = std::string_view(argv[index]);
        if (!UTF8Decoder::is_valid(text)) {
            return std::unexpected("process argument is not valid UTF-8");
        }
        result.emplace_back(text);
#endif
    }
    return result;
}

auto environment(const char* name) noexcept
    -> std::expected<std::optional<std::string>, std::string_view> {
#if defined(_WIN32)
    const auto name_length =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, nullptr, 0);
    if (name_length == 0) {
        return std::unexpected("environment variable name is not valid UTF-8");
    }
    auto wide_name = std::wstring(static_cast<std::size_t>(name_length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, name, -1, wide_name.data(), name_length)
        != name_length) {
        return std::unexpected("cannot encode environment variable name");
    }
    // Read the original Unicode environment rather than the CRT's narrow copy.
    SetLastError(ERROR_SUCCESS);
    const auto length = GetEnvironmentVariableW(wide_name.c_str(), nullptr, 0);
    if (length == 0) {
        const auto error = GetLastError();
        if (error == ERROR_ENVVAR_NOT_FOUND) {
            return std::nullopt;
        }
        if (error == ERROR_SUCCESS) {
            return std::string();
        }
        return std::unexpected("cannot read environment variable");
    }
    auto value = std::wstring(static_cast<std::size_t>(length), L'\0');
    SetLastError(ERROR_SUCCESS);
    const auto written = GetEnvironmentVariableW(wide_name.c_str(), value.data(), length);
    if (written == 0) {
        if (GetLastError() == ERROR_SUCCESS) {
            return std::string();
        }
        return std::unexpected("cannot read environment variable");
    }
    if (written >= length) {
        return std::unexpected("cannot read environment variable");
    }
    auto text = windows_text_to_utf8(value.c_str());
    if (!text) {
        return std::unexpected(text.error());
    }
    return std::move(*text);
#else
    const auto* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    const auto text = std::string_view(value);
    if (!UTF8Decoder::is_valid(text)) {
        return std::unexpected("environment variable is not valid UTF-8");
    }
    return std::string(text);
#endif
}

auto enable_binary_standard_streams() noexcept -> bool {
#if defined(_WIN32)
    return _setmode(0, _O_BINARY) != -1 && _setmode(1, _O_BINARY) != -1;
#else
    return true;
#endif
}

} // namespace process
