#pragma once

#include <cstdint>

inline auto cv_test_mutation_index_calls = std::int32_t {0};
inline auto cv_test_mutation_order = std::int32_t {0};

inline auto cv_test_mutation_reset_index_calls() noexcept -> void {
    cv_test_mutation_index_calls = 0;
}

inline auto cv_test_mutation_index_call_count() noexcept -> std::int32_t {
    return cv_test_mutation_index_calls;
}

inline auto cv_test_mutation_next_index() noexcept -> std::int32_t {
    ++cv_test_mutation_index_calls;
    return 0;
}

inline auto cv_test_mutation_reset_order() noexcept -> void {
    cv_test_mutation_order = 0;
}

inline auto cv_test_mutation_order_value() noexcept -> std::int32_t {
    return cv_test_mutation_order;
}

inline auto cv_test_mutation_ordered_index() noexcept -> std::int32_t {
    cv_test_mutation_order = cv_test_mutation_order * 10 + 1;
    return 0;
}

inline auto cv_test_mutation_ordered_value() noexcept -> std::int32_t {
    cv_test_mutation_order = cv_test_mutation_order * 10 + 2;
    return 3;
}
