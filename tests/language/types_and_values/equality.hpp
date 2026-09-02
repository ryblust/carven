#pragma once

#include <cstdint>

inline auto cv_test_equality_trace = std::int32_t {0};

inline auto cv_test_equality_reset_trace() noexcept -> void {
    cv_test_equality_trace = 0;
}

inline auto cv_test_equality_record(std::int32_t value) noexcept -> std::int32_t {
    cv_test_equality_trace = cv_test_equality_trace * 10 + value;
    return value;
}

inline auto cv_test_equality_trace_value() noexcept -> std::int32_t {
    return cv_test_equality_trace;
}
