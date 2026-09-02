#pragma once

#include <cstdint>

inline auto cv_test_catch_guard_calls = std::int32_t {0};

inline auto cv_test_reset_catch_guard_calls() noexcept -> void {
    cv_test_catch_guard_calls = 0;
}

inline auto cv_test_record_catch_guard() noexcept -> bool {
    ++cv_test_catch_guard_calls;
    return true;
}

inline auto cv_test_catch_guard_call_count() noexcept -> std::int32_t {
    return cv_test_catch_guard_calls;
}
