module carven:support.clock.impl;

import :support.clock;
import std;

namespace {

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

} // namespace

auto host_monotonic_now() noexcept -> std::int64_t {
    constexpr auto factor = NanosecondsPerTick::num;
    const auto ticks =
        static_cast<std::int64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    if (ticks < std::numeric_limits<std::int64_t>::lowest() / factor
        || ticks > std::numeric_limits<std::int64_t>::max() / factor) {
        std::terminate();
    }
    return ticks * factor;
}

auto host_monotonic_wait(std::int64_t deadline) noexcept -> void {
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
