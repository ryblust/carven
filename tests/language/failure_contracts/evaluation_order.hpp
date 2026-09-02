#pragma once

#include <cstdint>

inline auto cv_test_effect_trace = std::int32_t {0};

inline auto cv_test_reset_effect_trace() noexcept -> void {
    cv_test_effect_trace = 0;
}

inline auto cv_test_record_effect(std::int32_t marker) noexcept -> std::int32_t {
    cv_test_effect_trace = cv_test_effect_trace * 10 + marker;
    return marker;
}

inline auto cv_test_effect_trace_value() noexcept -> std::int32_t {
    return cv_test_effect_trace;
}
