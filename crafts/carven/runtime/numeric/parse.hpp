#pragma once

#include <carven/runtime/value/outcome.hpp>

#include <charconv>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace carven::runtime {

template<typename Number, typename Failure>
auto parse_number(std::string_view text, Failure invalid) noexcept -> Outcome<Number, Failure> {
    using Result = Outcome<Number, Failure>;
    if (text.empty()) {
        return Result::failure(invalid);
    }
    auto value = Number {};
    const auto parsed = [&]() noexcept {
        if constexpr (std::is_floating_point_v<Number>) {
            return std::from_chars(
                text.data(),
                text.data() + text.size(),
                value,
                std::chars_format::general
            );
        } else {
            return std::from_chars(text.data(), text.data() + text.size(), value);
        }
    }();
    if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()) {
        return Result::failure(invalid);
    }
    return Result::success(value);
}

} // namespace carven::runtime
