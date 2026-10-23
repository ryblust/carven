#pragma once

#include <carven/runtime/collection/slice.hpp>

#include <cstdint>

namespace carven::standard::collections {

struct DigestState final {
    std::uint64_t first;
    std::uint64_t second;
};

auto hash_bytes(runtime::Slice<std::uint8_t> bytes) noexcept -> std::uint64_t;

auto digest() noexcept -> DigestState;

auto update_digest(
    std::uint64_t first,
    std::uint64_t second,
    runtime::Slice<std::uint8_t> bytes
) noexcept -> DigestState;

} // namespace carven::standard::collections
