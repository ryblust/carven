#pragma once

#include <carven/runtime/collection/slice.hpp>

#include <cstdint>

namespace carven::standard::collections {

auto hash_bytes(runtime::Slice<std::uint8_t> bytes) noexcept -> std::uint64_t;

} // namespace carven::standard::collections
