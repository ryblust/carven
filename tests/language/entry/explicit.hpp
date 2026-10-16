#pragma once

#include <carven/generated/carven-test-runner.hpp>

#include <cstdint>
#include <cstdlib>

inline auto cv_test_entry_test_observed = false;
inline auto cv_test_entry_local_destructions = 0;
inline auto cv_test_entry_payloads_created = 0;
inline auto cv_test_entry_payloads_live = 0;
inline auto cv_test_entry_expected_payloads = 1;

inline auto cv_test_entry_mark_test_observed() noexcept -> void {
    cv_test_entry_test_observed = true;
}

inline auto cv_test_entry_run_tests() noexcept -> std::int32_t {
    return static_cast<std::int32_t>(carven::runtime::run_generated_tests());
}

class EntryLocalOwner final {
public:
    EntryLocalOwner() noexcept = default;
    EntryLocalOwner(const EntryLocalOwner&) = delete;
    auto operator=(const EntryLocalOwner&) -> EntryLocalOwner& = delete;

    ~EntryLocalOwner() { ++cv_test_entry_local_destructions; }
};

inline auto cv_test_entry_begin(std::int32_t expected) noexcept -> EntryLocalOwner {
    cv_test_entry_expected_payloads = expected;
    return EntryLocalOwner {};
}

class EntryPayload final {
public:
    EntryPayload() noexcept {
        ++cv_test_entry_payloads_created;
        ++cv_test_entry_payloads_live;
    }

    EntryPayload(const EntryPayload&) noexcept { ++cv_test_entry_payloads_live; }

    EntryPayload(EntryPayload&&) noexcept { ++cv_test_entry_payloads_live; }

    auto operator=(const EntryPayload&) -> EntryPayload& = delete;
    auto operator=(EntryPayload&&) -> EntryPayload& = delete;

    ~EntryPayload() { --cv_test_entry_payloads_live; }
};

struct EntryCompletionCheck final {
    ~EntryCompletionCheck() {
        if (!cv_test_entry_test_observed
            || cv_test_entry_local_destructions != 1
            || cv_test_entry_payloads_created != cv_test_entry_expected_payloads
            || cv_test_entry_payloads_live != 0) {
            std::abort();
        }
    }
};

inline const auto cv_test_entry_completion_check = EntryCompletionCheck {};
