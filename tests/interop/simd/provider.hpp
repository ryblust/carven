#pragma once

#include <cstddef>

auto simd_float_storage() noexcept -> bool;
auto simd_guard_pages_available() noexcept -> bool;
// Zero indicates success. Nonzero identifies a setup, width, or cleanup failure.
auto simd_guarded_tails() noexcept -> std::size_t;
auto simd_utf8_encodings() noexcept -> bool;
auto simd_utf8_guarded() noexcept -> bool;
