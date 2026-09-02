#pragma once

#include <cstdint>

inline auto cv_test_call_evaluation_order = std::int32_t {0};

inline auto cv_test_call_reset_order() noexcept -> void {
    cv_test_call_evaluation_order = 0;
}

inline auto cv_test_call_record(std::int32_t marker) noexcept -> std::int32_t {
    cv_test_call_evaluation_order = cv_test_call_evaluation_order * 10 + marker;
    return marker;
}

inline auto cv_test_call_order() noexcept -> std::int32_t {
    return cv_test_call_evaluation_order;
}
