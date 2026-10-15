module carven:support.clock;

import std;

auto host_monotonic_now() noexcept -> std::int64_t;
auto host_monotonic_wait(std::int64_t deadline) noexcept -> void;
