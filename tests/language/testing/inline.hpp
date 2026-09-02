#pragma once

#include <cstdint>

inline auto cv_test_inline_operation_trace = std::int32_t {0};

inline auto cv_test_inline_reset_trace() noexcept -> void {
    cv_test_inline_operation_trace = 0;
}

inline auto cv_test_inline_condition(std::int32_t digit, bool value) noexcept -> bool {
    cv_test_inline_operation_trace = cv_test_inline_operation_trace * 10 + digit;
    return value;
}

inline auto cv_test_inline_mark(std::int32_t digit) noexcept -> void {
    cv_test_inline_operation_trace = cv_test_inline_operation_trace * 10 + digit;
}

inline auto cv_test_inline_trace() noexcept -> std::int32_t {
    return cv_test_inline_operation_trace;
}
