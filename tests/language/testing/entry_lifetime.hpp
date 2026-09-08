#pragma once

#include <cstdlib>

inline auto cv_test_entry_calls = 0;
inline auto cv_test_entry_local_destructions = 0;
inline auto cv_test_entry_payloads_created = 0;
inline auto cv_test_entry_payloads_live = 0;

class EntryCompletionCheck final {
public:
    ~EntryCompletionCheck() {
        if (cv_test_entry_calls != 1 || cv_test_entry_local_destructions != 1) {
            std::abort();
        }
        const auto* expected = std::getenv("CARVEN_ENTRY_TEST_PAYLOADS");
        if (expected == nullptr
            || expected[0] < '0'
            || expected[0] > '2'
            || expected[1] != '\0'
            || cv_test_entry_payloads_created != expected[0] - '0'
            || cv_test_entry_payloads_live != 0) {
            std::abort();
        }
    }
};

inline const auto cv_test_entry_completion_check = EntryCompletionCheck {};

class EntryLocalOwner final {
public:
    EntryLocalOwner() noexcept = default;
    EntryLocalOwner(const EntryLocalOwner&) = delete;
    auto operator=(const EntryLocalOwner&) -> EntryLocalOwner& = delete;

    ~EntryLocalOwner() { ++cv_test_entry_local_destructions; }
};

inline auto cv_test_entry_begin() noexcept -> EntryLocalOwner {
    ++cv_test_entry_calls;
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
