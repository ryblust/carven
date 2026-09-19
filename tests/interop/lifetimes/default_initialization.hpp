#pragma once

#include <cstdint>

namespace default_probe {

inline auto trace = std::int32_t {0};
inline auto live = std::int32_t {0};

inline auto mark(std::int32_t value) noexcept -> std::int32_t {
    trace = trace * 10 + value;
    return value;
}

inline auto reset() noexcept -> void {
    trace = 0;
}

inline auto observed() noexcept -> std::int32_t {
    return trace;
}

inline auto owners() noexcept -> std::int32_t {
    return live;
}

struct Item final {
    Item() noexcept {
        mark(1);
        ++live;
    }

    Item(const Item&) noexcept {
        mark(3);
        ++live;
    }

    Item(Item&&) noexcept {
        mark(4);
        ++live;
    }

    ~Item() { --live; }
};

struct Fixed final {
    Fixed() noexcept {
        mark(2);
        ++live;
    }

    Fixed(const Fixed&) = delete;
    Fixed(Fixed&&) = delete;

    ~Fixed() { --live; }
};

} // namespace default_probe
