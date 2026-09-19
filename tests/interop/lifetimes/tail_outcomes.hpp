#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

namespace tail_outcome_probe {

inline auto events = std::string();
inline auto moves = std::int32_t {0};

inline auto record(char event, std::int32_t value) noexcept -> void {
    events += event;
    events += std::to_string(value);
    events += ' ';
}

inline auto reset() noexcept -> void {
    events.clear();
    moves = 0;
}

inline auto move_count() noexcept -> std::int32_t {
    return moves;
}

inline auto matches(std::string_view expected) noexcept -> bool {
    if (events == expected) {
        return true;
    }
    std::fprintf(stderr, "tail outcome events: %s\n", events.c_str());
    return false;
}

struct CopyTrivial final {
    std::int32_t value;

    explicit CopyTrivial(std::int32_t source) noexcept
        : value(source) {}

    CopyTrivial(const CopyTrivial&) = default;

    CopyTrivial(CopyTrivial&& source) noexcept
        : value(source.value) {
        ++moves;
    }

    auto get() const noexcept -> std::int32_t { return value; }
};

static_assert(std::is_trivially_copy_constructible_v<CopyTrivial>);
static_assert(std::is_trivially_destructible_v<CopyTrivial>);
static_assert(!std::is_trivially_move_constructible_v<CopyTrivial>);

struct Self final {
    const Self* construction_address;
    char padding[64];

    Self() noexcept
        : construction_address(this),
          padding {} {}

    auto at_construction_address() const noexcept -> bool { return construction_address == this; }
};

static_assert(std::is_trivially_copy_constructible_v<Self>);
static_assert(std::is_trivially_move_constructible_v<Self>);
static_assert(std::is_trivially_destructible_v<Self>);

struct Tracked final {
    std::int32_t value;

    explicit Tracked(std::int32_t source) noexcept
        : value(source) {
        record('c', value);
    }

    Tracked(const Tracked& source) noexcept
        : value(source.value) {
        record('p', value);
    }

    Tracked(Tracked&& source) noexcept
        : value(source.value) {
        record('m', value);
        source.value = -source.value;
    }

    ~Tracked() { record(value < 0 ? 's' : 'd', value); }

    auto get() const noexcept -> std::int32_t {
        record('r', value);
        return value;
    }
};

struct Guard final {
    std::int32_t value;

    explicit Guard(std::int32_t source) noexcept
        : value(source) {
        record('G', value);
    }

    Guard(const Guard&) = delete;
    Guard(Guard&&) = delete;

    ~Guard() { record('g', value); }
};

} // namespace tail_outcome_probe
