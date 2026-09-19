#pragma once

#include <carven/runtime/array.hpp>
#include <carven/runtime/callable.hpp>
#include <carven/runtime/outcome.hpp>
#include <carven/runtime/string.hpp>
#include <carven/runtime/testing.hpp>

#include <cstdint>
#include <memory>
#include <string_view>
#include <utility>

namespace boundary {

using Owned = std::unique_ptr<std::int32_t>;

} // namespace boundary

inline auto contract_text(const carven::runtime::String& value) noexcept
    -> carven::runtime::String {
    return value;
}

inline auto contract_replace(carven::runtime::String& value) noexcept -> void {
    value = carven::runtime::String::from_str("replaced");
}

inline auto contract_take(boundary::Owned value) noexcept -> boundary::Owned {
    return value;
}

inline auto contract_owner() noexcept -> boundary::Owned {
    return std::make_unique<std::int32_t>(42);
}

inline auto contract_pointer(std::int32_t* value) noexcept -> std::int32_t* {
    ++*value;
    return value;
}

template<typename Value>
inline auto contract_record(const Value& value) noexcept -> Value {
    return value;
}

template<typename Callback>
inline auto contract_callback(const Callback& callback) noexcept {
    return carven::runtime::native_test_result(callback(7));
}

template<typename Failure>
inline auto contract_failure(bool fail, const Failure& error) noexcept
    -> carven::runtime::Outcome<carven::runtime::String, Failure> {
    using Result = carven::runtime::Outcome<carven::runtime::String, Failure>;
    if (fail) {
        return Result::failure(error);
    }
    return Result::success_from([]() noexcept {
        return carven::runtime::String::from_str("success");
    });
}

auto contract_consumer() noexcept -> bool;

inline auto contract_owned_value(const boundary::Owned& value) noexcept -> std::int32_t {
    return *value;
}

inline auto contract_address(std::int32_t& value) noexcept -> std::int32_t* {
    return &value;
}

template<typename Value>
inline auto contract_identity(const Value& value) noexcept -> Value {
    return value;
}

template<typename Failure>
inline auto contract_void(bool fail, const Failure& error) noexcept
    -> carven::runtime::Outcome<void, Failure> {
    using Result = carven::runtime::Outcome<void, Failure>;
    if (fail) {
        return Result::failure(error);
    }
    return Result::success();
}

inline auto contract_access(std::int32_t&&) noexcept -> std::int32_t {
    return 1;
}

inline auto contract_access(const std::int32_t&) noexcept -> std::int32_t {
    return 2;
}
