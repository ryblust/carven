#pragma once

#include <carven/generated/carven-test-runner.hpp>

#include <cstdint>
#include <cstdlib>

inline auto cv_test_entry_test_observed = false;

inline auto cv_test_entry_mark_test_observed() noexcept -> void {
    cv_test_entry_test_observed = true;
}

inline auto cv_test_entry_run_tests() noexcept -> std::int32_t {
    return static_cast<std::int32_t>(carven::testing::run_generated_tests());
}

inline auto cv_test_entry_observe(bool condition) noexcept -> void {
    if (!condition) {
        std::abort();
    }
}

inline auto cv_test_entry_test_was_observed() noexcept -> bool {
    return cv_test_entry_test_observed;
}
