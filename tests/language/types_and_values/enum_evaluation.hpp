#pragma once

#include <cstdint>

inline auto cv_test_sum_evaluation_order = std::int32_t {0};
inline auto cv_test_enum_match_subject_evaluations = std::int32_t {0};

inline auto cv_test_reset_sum_evaluation_order() noexcept -> void {
    cv_test_sum_evaluation_order = 0;
}

inline auto cv_test_sum_record(std::int32_t marker) noexcept -> std::int32_t {
    cv_test_sum_evaluation_order = cv_test_sum_evaluation_order * 10 + marker;
    return marker;
}

inline auto cv_test_sum_evaluation_order_value() noexcept -> std::int32_t {
    return cv_test_sum_evaluation_order;
}

inline auto cv_test_reset_match_subject_evaluations() noexcept -> void {
    cv_test_enum_match_subject_evaluations = 0;
}

inline auto cv_test_record_match_subject() noexcept -> std::int32_t {
    ++cv_test_enum_match_subject_evaluations;
    return cv_test_enum_match_subject_evaluations;
}

inline auto cv_test_match_subject_evaluation_count() noexcept -> std::int32_t {
    return cv_test_enum_match_subject_evaluations;
}
