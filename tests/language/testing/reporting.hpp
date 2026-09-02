#pragma once

#include <cstdint>

inline auto cv_test_inline_reporting_flags = std::uint32_t {0};

inline auto cv_test_reporting_mark(std::int32_t bit) noexcept -> void {
    cv_test_inline_reporting_flags |= std::uint32_t {1} << bit;
}

inline auto cv_test_reporting_condition(std::int32_t bit, bool value) noexcept -> bool {
    cv_test_reporting_mark(bit);
    return value;
}

extern "C" auto cv_test_reporting_observed_flags() noexcept -> std::uint32_t {
    return cv_test_inline_reporting_flags;
}
