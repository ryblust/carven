#pragma once
#include <carven/runtime/runtime.hpp>
#include <cstdint>

class TailOwner final {
    std::int32_t digit;

public:
    explicit TailOwner(std::int32_t value) noexcept;
    TailOwner(const TailOwner&) = delete;
    TailOwner(TailOwner&&) = delete;
    ~TailOwner() noexcept;
};

auto tail_owner(std::int32_t digit) noexcept -> TailOwner;
auto tail_reset() noexcept -> void;
auto tail_mark(std::int32_t digit) noexcept -> void;
auto tail_actual(std::int32_t value, std::int32_t digit) noexcept -> std::int32_t;
auto tail_trace() noexcept -> std::int32_t;
auto tail_address(std::int32_t& value) noexcept -> void;
auto tail_addresses() noexcept -> std::int32_t;
auto tail_native() noexcept -> carven::runtime::async::Operation<std::int32_t>;
