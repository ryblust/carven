#include "fixed.hpp"

#include <cstdio>

namespace {

struct Counters final {
    std::int32_t created;
    std::int32_t destroyed;
    std::int32_t live;
    std::int32_t trace;
    const Fixed* active;
};

Counters counters {
    .created = 0,
    .destroyed = 0,
    .live = 0,
    .trace = 0,
    .active = nullptr,
};

} // namespace

Fixed::Fixed() noexcept
    : construction_address(this) {
    ++counters.created;
    ++counters.live;
    counters.trace = counters.trace * 10 + 1;
    counters.active = this;
}

Fixed::~Fixed() noexcept {
    ++counters.destroyed;
    --counters.live;
    counters.trace = counters.trace * 10 + 2;
    if (counters.active == this) {
        counters.active = nullptr;
    }
}

auto Fixed::at_construction_address() const noexcept -> bool {
    return construction_address == this;
}

auto make_fixed() noexcept -> Fixed {
    return Fixed {};
}

auto fixed_created() noexcept -> std::int32_t {
    return counters.created;
}

auto fixed_destroyed() noexcept -> std::int32_t {
    return counters.destroyed;
}

auto fixed_live() noexcept -> std::int32_t {
    return counters.live;
}

auto fixed_trace() noexcept -> std::int32_t {
    return counters.trace;
}

auto fixed_identity_stable() noexcept -> std::int32_t {
    return counters.active != nullptr && counters.active->at_construction_address() ? 1 : 0;
}

auto fixed_report() noexcept -> void {
    std::printf(
        "%d %d %d %d %d\n",
        static_cast<int>(fixed_created()),
        static_cast<int>(fixed_destroyed()),
        static_cast<int>(fixed_live()),
        static_cast<int>(fixed_trace()),
        static_cast<int>(fixed_identity_stable())
    );
}
