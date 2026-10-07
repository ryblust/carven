#include "tail.hpp"
#include <array>
#include <cstddef>

namespace {
std::int32_t trace = 0;
std::array<const void*, 8> addresses {};
std::size_t address_count = 0;
}

TailOwner::TailOwner(std::int32_t value) noexcept
    : digit(value) {}

TailOwner::~TailOwner() noexcept {
    tail_mark(digit);
}

auto tail_owner(std::int32_t digit) noexcept -> TailOwner {
    return TailOwner(digit);
}

auto tail_reset() noexcept -> void {
    trace = 0;
    address_count = 0;
}

auto tail_mark(std::int32_t digit) noexcept -> void {
    trace = trace * 10 + digit;
}

auto tail_actual(std::int32_t value, std::int32_t digit) noexcept -> std::int32_t {
    tail_mark(digit);
    return value;
}

auto tail_trace() noexcept -> std::int32_t {
    return trace;
}

auto tail_address(std::int32_t& value) noexcept -> void {
    for (std::size_t index = 0; index < address_count; ++index) {
        if (addresses[index] == &value) {
            return;
        }
    }
    addresses[address_count++] = &value;
}

auto tail_addresses() noexcept -> std::int32_t {
    return static_cast<std::int32_t>(address_count);
}

namespace {
struct FrameToken final {
    bool active = true;
    FrameToken() = default;
    FrameToken(const FrameToken&) = delete;

    FrameToken(FrameToken&& source) noexcept
        : active(source.active) {
        source.active = false;
    }

    ~FrameToken() noexcept {
        if (active) {
            tail_mark(4);
        }
    }
};

auto native_frame(FrameToken token) noexcept -> carven::runtime::async::Operation<std::int32_t> {
    static_cast<void>(token);
    co_return carven::runtime::async::Completion<std::int32_t>::success(1);
}
}

auto tail_native() noexcept -> carven::runtime::async::Operation<std::int32_t> {
    // A movable frame parameter keeps observable cleanup until its operation
    // owner ends the enclosing full-expression.
    return native_frame(FrameToken {});
}
