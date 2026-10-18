#pragma once

#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/diagnostics/trap.hpp>
#include <carven/runtime/display/display.hpp>
#include <carven/runtime/text/text.hpp>

#include <concepts>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <corecrt_startup.h>
#include <windows.h>
#endif

namespace carven::runtime {

// Generated process entries pass main's argc/argv. Windows reads the CRT's wide
// arguments; POSIX rejects invalid UTF-8. The returned strings own their bytes.
inline auto entry_args(int argc, [[maybe_unused]] const char* const* argv) noexcept
    -> Vector<String> {
#if defined(_WIN32)
    // Narrow argv uses the system code page and may already have lost Unicode.
    // Let the CRT parse wide arguments, preserving quoting and empty arguments.
    if (_configure_wide_argv(_crt_argv_unexpanded_arguments) != 0) {
        trap("could not read process arguments", SourceSite::native());
    }
    argc = __argc;
#endif
    const auto count = argc > 1 ? static_cast<std::size_t>(argc - 1) : std::size_t {0};
    auto result = Vector<String>::with_capacity(count);
    for (auto index = std::size_t {0}; index < count; ++index) {
#if defined(_WIN32)
        const auto* argument = __wargv[index + 1];
        const auto length = WideCharToMultiByte(
            CP_UTF8,
            WC_ERR_INVALID_CHARS,
            argument,
            -1,
            nullptr,
            0,
            nullptr,
            nullptr
        );
        if (length == 0) {
            trap("process argument is not valid UTF-16", SourceSite::native());
        }
        auto text = std::string(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(
                CP_UTF8,
                WC_ERR_INVALID_CHARS,
                argument,
                -1,
                text.data(),
                length,
                nullptr,
                nullptr
            )
            != length) {
            trap("could not encode process argument as UTF-8", SourceSite::native());
        }
        text.pop_back();
        result.push(String::from_utf8(std::move(text)));
#else
        const auto text = std::string_view(argv[index + 1]);
        checked_utf8(text, SourceSite::native());
        result.push(String::from_str(text));
#endif
    }
    return result;
}

namespace detail {

CARVEN_RUNTIME_COLD inline auto write_entry_failure(
    std::string_view name,
    std::string_view payload,
    SourceSite site
) noexcept -> void {
    auto reason = std::string("failure '");
    reason += name;
    reason += "' escaped the program entry";
    begin_report(reason, site);
    write_report_field("failure:", payload);
    std::fputs("  note: program exited with a failure status\n\n", stderr);
    std::fflush(stderr);
}

} // namespace detail

// Reports the failure alternative held by a completed entry outcome. The entry
// has already run its cleanup, so the process still exits normally.
template<typename Failure, typename Emit>
    requires std::invocable<Emit&, DisplayWriter&, const Failure&, std::size_t>
auto report_entry_failure(
    const Failure* failure,
    std::string_view name,
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): Synchronous display borrows the emitter under its lvalue invocation contract.
    Emit&& emit,
    SourceSite site
) noexcept -> void {
    if (failure == nullptr) {
        return;
    }
    auto writer = DisplayWriter();
    std::invoke(emit, writer, *failure, std::size_t {0});
    detail::write_entry_failure(name, writer.result(), site);
}

} // namespace carven::runtime

namespace carven::runtime::execution {

inline auto report_entry_cancelled(SourceSite site) noexcept -> void {
    carven::runtime::detail::begin_report("cancellation escaped the program entry", site);
    std::fputs("  note: program exited with a failure status\n\n", stderr);
    std::fflush(stderr);
}

} // namespace carven::runtime::execution
