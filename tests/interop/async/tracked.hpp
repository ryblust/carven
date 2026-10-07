#pragma once

#include <cstdint>

class Tracked final {
public:
    Tracked() noexcept;
    Tracked(const Tracked& source) noexcept;
    Tracked(Tracked&& source) noexcept;
    auto operator=(const Tracked&) -> Tracked& = delete;
    auto operator=(Tracked&&) -> Tracked& = delete;
    ~Tracked() noexcept;

private:
    bool moved_from;
};

auto make_tracked() noexcept -> Tracked;

auto tracked_created() noexcept -> std::int32_t;
auto tracked_copied() noexcept -> std::int32_t;
auto tracked_moved() noexcept -> std::int32_t;
auto tracked_destroyed() noexcept -> std::int32_t;
auto tracked_live() noexcept -> std::int32_t;
auto tracked_trace() noexcept -> std::int32_t;
auto tracked_report() noexcept -> void;
