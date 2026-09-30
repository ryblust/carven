#pragma once

#include "display/display.hpp"
#include "trap.hpp"
#include "utf.hpp"

#include <cstddef>
#include <cstdio>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>

namespace carven::runtime {

constexpr auto entry_args(int argc, const char* const* argv) noexcept -> auto {
    const auto count = argc > 1 ? static_cast<std::size_t>(argc - 1) : std::size_t {0};
    for (auto index = std::size_t {0}; index < count; ++index) {
        checked_utf8(std::string_view(argv[index + 1]), SourceSite::native());
    }
    return std::views::iota(std::size_t {0}, count)
        | std::views::transform([argv](std::size_t index) noexcept {
               return std::pair {index, std::string_view(argv[index + 1])};
           });
}

using EntryArgs = decltype(entry_args(0, nullptr));

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
auto report_entry_failure(
    const Failure* failure,
    std::string_view name,
    Emit emit,
    SourceSite site
) noexcept -> void {
    if (failure == nullptr) {
        return;
    }
    auto writer = DisplayWriter();
    emit(writer, *failure);
    detail::write_entry_failure(name, writer.result(), site);
}

} // namespace carven::runtime
