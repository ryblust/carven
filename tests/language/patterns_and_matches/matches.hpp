#pragma once

#include <cstdint>

inline auto cv_match_subject_evaluations = std::int32_t {0};
inline auto cv_match_dead_guard_evaluations = std::int32_t {0};

inline auto cv_match_reset_evaluations() noexcept -> void {
    cv_match_subject_evaluations = 0;
    cv_match_dead_guard_evaluations = 0;
}

inline auto cv_match_subject_once() noexcept -> bool {
    ++cv_match_subject_evaluations;
    return true;
}

inline auto cv_match_dead_guard() noexcept -> bool {
    ++cv_match_dead_guard_evaluations;
    return true;
}

inline auto cv_match_subject_evaluation_count() noexcept -> std::int32_t {
    return cv_match_subject_evaluations;
}

inline auto cv_match_dead_guard_evaluation_count() noexcept -> std::int32_t {
    return cv_match_dead_guard_evaluations;
}
