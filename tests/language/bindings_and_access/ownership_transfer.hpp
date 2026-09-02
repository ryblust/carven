#pragma once

#include <cstdint>

inline auto cv_test_take_order = std::int32_t {0};

inline auto cv_test_take_reset_order() noexcept -> void {
    cv_test_take_order = 0;
}

inline auto cv_test_take_record(std::int32_t marker) noexcept -> std::int32_t {
    cv_test_take_order = cv_test_take_order * 10 + marker;
    return marker;
}

inline auto cv_test_take_order_value() noexcept -> std::int32_t {
    return cv_test_take_order;
}
