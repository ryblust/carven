#pragma once

#include "tail_outcomes.hpp"

#include <cstdint>

namespace failure_receiver_probe {

inline auto mark(std::int32_t value) noexcept -> void {
    tail_outcome_probe::record('H', value);
}

} // namespace failure_receiver_probe
