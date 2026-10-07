#include "tracked.hpp"

#include <cstdio>

namespace {

struct Counters final {
    std::int32_t created;
    std::int32_t copied;
    std::int32_t moved;
    std::int32_t destroyed;
    std::int32_t live;
    std::int32_t trace;
};

Counters counters {
    .created = 0,
    .copied = 0,
    .moved = 0,
    .destroyed = 0,
    .live = 0,
    .trace = 0,
};

} // namespace

Tracked::Tracked() noexcept
    : moved_from(false) {
    ++counters.created;
    ++counters.live;
    counters.trace = counters.trace * 10 + 1;
}

Tracked::Tracked(const Tracked&) noexcept
    : moved_from(false) {
    ++counters.copied;
    ++counters.live;
    counters.trace = counters.trace * 10 + 5;
}

Tracked::Tracked(Tracked&& source) noexcept
    : moved_from(false) {
    source.moved_from = true;
    ++counters.moved;
    ++counters.live;
    counters.trace = counters.trace * 10 + 2;
}

Tracked::~Tracked() noexcept {
    ++counters.destroyed;
    --counters.live;
    counters.trace = counters.trace * 10 + (moved_from ? 3 : 4);
}

auto make_tracked() noexcept -> Tracked {
    return Tracked {};
}

auto tracked_created() noexcept -> std::int32_t {
    return counters.created;
}

auto tracked_copied() noexcept -> std::int32_t {
    return counters.copied;
}

auto tracked_moved() noexcept -> std::int32_t {
    return counters.moved;
}

auto tracked_destroyed() noexcept -> std::int32_t {
    return counters.destroyed;
}

auto tracked_live() noexcept -> std::int32_t {
    return counters.live;
}

auto tracked_trace() noexcept -> std::int32_t {
    return counters.trace;
}

auto tracked_report() noexcept -> void {
    std::printf(
        "%d %d %d %d %d %d\n",
        static_cast<int>(tracked_created()),
        static_cast<int>(tracked_copied()),
        static_cast<int>(tracked_moved()),
        static_cast<int>(tracked_destroyed()),
        static_cast<int>(tracked_live()),
        static_cast<int>(tracked_trace())
    );
}
