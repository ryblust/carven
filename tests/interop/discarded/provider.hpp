#pragma once

inline auto discarded_invalid_scalar() noexcept -> char32_t {
    return static_cast<char32_t>(0xd800);
}
