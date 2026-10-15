#pragma once

#include <chrono>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <ratio>
#include <thread>
#include <type_traits>

namespace carven::runtime::execution::detail {

using SteadyDuration = std::chrono::steady_clock::duration;
using SteadyRep = SteadyDuration::rep;
using NanosecondsPerTick = std::ratio_divide<SteadyDuration::period, std::nano>;

static_assert(
    std::is_integral_v<SteadyRep>
        && std::is_signed_v<SteadyRep>
        && std::numeric_limits<SteadyRep>::digits == 63,
    "execution timers require a signed 64-bit integral steady-clock representation"
);

static_assert(
    NanosecondsPerTick::den == 1 && NanosecondsPerTick::num > 0,
    "execution timers require steady-clock ticks containing whole nanoseconds"
);

inline auto steady_now() noexcept -> std::int64_t {
    constexpr auto factor = NanosecondsPerTick::num;
    const auto ticks =
        static_cast<std::int64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    if (ticks < std::numeric_limits<std::int64_t>::lowest() / factor
        || ticks > std::numeric_limits<std::int64_t>::max() / factor) {
        std::terminate();
    }
    return ticks * factor;
}

inline auto steady_wait_until(std::int64_t deadline) noexcept -> void {
    constexpr auto factor = NanosecondsPerTick::num;
    auto ticks = deadline / factor;
    // Round toward the later clock tick, including negative epoch offsets.
    if (deadline % factor > 0) {
        ++ticks;
    }
    std::this_thread::sleep_until(
        std::chrono::steady_clock::time_point(SteadyDuration(static_cast<SteadyRep>(ticks)))
    );
}

// Poll uses signed milliseconds; compute the difference without signed overflow.
inline auto steady_poll_timeout(std::optional<std::int64_t> deadline) noexcept -> int {
    if (!deadline) {
        return -1;
    }
    const auto instant = steady_now();
    if (*deadline <= instant) {
        return 0;
    }
    const auto remaining =
        static_cast<std::uint64_t>(*deadline) - static_cast<std::uint64_t>(instant);
    const auto milliseconds = remaining / 1000000 + (remaining % 1000000 != 0);
    return milliseconds > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(milliseconds);
}

struct Clock final {
    void* state;
    std::int64_t (*now)(void*) noexcept;
    void (*wait_until)(void*, std::int64_t) noexcept;
};

inline auto default_clock() noexcept -> Clock {
    return Clock {
        .state = nullptr,
        .now = [](void*) noexcept { return steady_now(); },
        .wait_until = [](void*, std::int64_t deadline) noexcept { steady_wait_until(deadline); }
    };
}

} // namespace carven::runtime::execution::detail
