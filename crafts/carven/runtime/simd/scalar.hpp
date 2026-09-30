#pragma once

// Portable realization of the backend contract for targets without NEON or
// AVX2. Include simd.hpp instead.
#if !defined(CARVEN_SIMD_INLINE)
#error "include carven/runtime/simd/simd.hpp instead of a SIMD backend header"
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace carven::runtime::simd {
inline namespace scalar {
namespace backend {

// Float lanes keep their native object representation. Masks are canonical, so
// every byte of a true lane is 255 regardless of byte order.
template<std::size_t Bytes>
struct Storage final {
    std::array<std::uint8_t, Bytes> bytes;
};

template<std::size_t Bytes, typename Operation>
CARVEN_SIMD_INLINE auto map(Storage<Bytes> a, Storage<Bytes> b, Operation operation) noexcept
    -> Storage<Bytes> {
    auto result = Storage<Bytes> {};
    for (auto i = std::size_t {0}; i < Bytes; ++i) {
        result.bytes[i] = static_cast<std::uint8_t>(operation(a.bytes[i], b.bytes[i]));
    }
    return result;
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto lane(Storage<Bytes> value, std::size_t index) noexcept -> float {
    auto result = 0.0f;
    std::memcpy(&result, value.bytes.data() + index * sizeof(float), sizeof(float));
    return result;
}

template<std::size_t Bytes, typename Operation>
CARVEN_SIMD_INLINE auto map_floats(Storage<Bytes> a, Storage<Bytes> b, Operation operation) noexcept
    -> Storage<Bytes> {
    auto result = Storage<Bytes> {};
    for (auto i = std::size_t {0}; i < Bytes / sizeof(float); ++i) {
        const auto value = static_cast<float>(operation(lane(a, i), lane(b, i)));
        std::memcpy(result.bytes.data() + i * sizeof(float), &value, sizeof(float));
    }
    return result;
}

template<std::size_t Bytes, typename Operation>
CARVEN_SIMD_INLINE auto compare_floats(
    Storage<Bytes> a,
    Storage<Bytes> b,
    Operation operation
) noexcept -> Storage<Bytes> {
    auto result = Storage<Bytes> {};
    for (auto i = std::size_t {0}; i < Bytes / sizeof(float); ++i) {
        const auto fill = operation(lane(a, i), lane(b, i)) ? std::uint8_t {255} : std::uint8_t {0};
        for (auto j = std::size_t {0}; j < sizeof(float); ++j) {
            result.bytes[i * sizeof(float) + j] = fill;
        }
    }
    return result;
}

template<std::size_t Bytes>
using Value = Storage<Bytes>;

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL splat_bytes(std::uint8_t element) noexcept
    -> Value<Bytes> {
    auto result = Value<Bytes> {};
    result.bytes.fill(element);
    return result;
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL splat_floats(float element) noexcept -> Value<Bytes> {
    auto result = Value<Bytes> {};
    for (auto i = std::size_t {0}; i < Bytes / sizeof(float); ++i) {
        std::memcpy(result.bytes.data() + i * sizeof(float), &element, sizeof(float));
    }
    return result;
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL load(const void* data) noexcept -> Value<Bytes> {
    auto result = Value<Bytes> {};
    std::memcpy(result.bytes.data(), data, Bytes);
    return result;
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL store(void* data, Value<Bytes> value) noexcept -> void {
    std::memcpy(data, value.bytes.data(), Bytes);
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL add_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](std::uint8_t a, std::uint8_t b) noexcept { return a + b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sub_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](std::uint8_t a, std::uint8_t b) noexcept { return a - b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_and(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](std::uint8_t a, std::uint8_t b) noexcept { return a & b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_or(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](std::uint8_t a, std::uint8_t b) noexcept { return a | b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_xor(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](std::uint8_t a, std::uint8_t b) noexcept { return a ^ b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_not(Value<Bytes> a) noexcept -> Value<Bytes> {
    return bit_xor<Bytes>(a, splat_bytes<Bytes>(255));
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL equal_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](std::uint8_t a, std::uint8_t b) noexcept {
        return a == b ? 255 : 0;
    });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](std::uint8_t a, std::uint8_t b) noexcept {
        return a < b ? 255 : 0;
    });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL add_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map_floats<Bytes>(a, b, [](float a, float b) noexcept { return a + b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sub_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map_floats<Bytes>(a, b, [](float a, float b) noexcept { return a - b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL mul_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map_floats<Bytes>(a, b, [](float a, float b) noexcept { return a * b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL div_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map_floats<Bytes>(a, b, [](float a, float b) noexcept { return a / b; });
}

// Ordered comparisons: every comparison involving NaN is false.
template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL equal_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<Bytes>(a, b, [](float a, float b) noexcept { return a == b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<Bytes>(a, b, [](float a, float b) noexcept { return a < b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_equal_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<Bytes>(a, b, [](float a, float b) noexcept { return a <= b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
select(Value<Bytes> mask, Value<Bytes> yes, Value<Bytes> no) noexcept -> Value<Bytes> {
    auto result = Value<Bytes> {};
    for (auto i = std::size_t {0}; i < Bytes; ++i) {
        result.bytes[i] = mask.bytes[i] != 0 ? yes.bytes[i] : no.bytes[i];
    }
    return result;
}

// Every index at or beyond the logical width selects zero.
template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL lookup(Value<Bytes> table, Value<Bytes> indices) noexcept
    -> Value<Bytes> {
    auto result = Value<Bytes> {};
    for (auto i = std::size_t {0}; i < Bytes; ++i) {
        const auto index = std::size_t {indices.bytes[i]};
        result.bytes[i] = index < Bytes ? table.bytes[index] : std::uint8_t {0};
    }
    return result;
}

template<std::size_t Bytes, std::size_t Count, bool Left>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL shift(Value<Bytes> a) noexcept -> Value<Bytes> {
    static_assert(Count < 8);
    auto result = Value<Bytes> {};
    for (auto i = std::size_t {0}; i < Bytes; ++i) {
        const auto byte = static_cast<unsigned>(a.bytes[i]);
        result.bytes[i] = static_cast<std::uint8_t>(Left ? byte << Count : byte >> Count);
    }
    return result;
}

template<std::size_t Bytes, bool Float>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bits(Value<Bytes> a) noexcept -> std::uint32_t {
    constexpr auto lane_bytes = Float ? sizeof(float) : std::size_t {1};
    auto result = std::uint32_t {0};
    for (auto i = std::size_t {0}; i < Bytes / lane_bytes; ++i) {
        if ((a.bytes[i * lane_bytes] & 0x80u) != 0) {
            result |= std::uint32_t {1} << i;
        }
    }
    return result;
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL any(Value<Bytes> a) noexcept -> bool {
    for (const auto byte : a.bytes) {
        if (byte != 0) {
            return true;
        }
    }
    return false;
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL all(Value<Bytes> a) noexcept -> bool {
    for (const auto byte : a.bytes) {
        if (byte != 255) {
            return false;
        }
    }
    return true;
}

template<std::size_t Bytes, bool Float>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL count(Value<Bytes> a) noexcept -> std::size_t {
    constexpr auto lane_bytes = Float ? sizeof(float) : std::size_t {1};
    auto result = std::size_t {0};
    for (auto i = std::size_t {0}; i < Bytes / lane_bytes; ++i) {
        result += a.bytes[i * lane_bytes] != 0 ? 1 : 0;
    }
    return result;
}

// Offset is a byte offset in 1..Bytes-1; the public layer handles both ends.
template<std::size_t Bytes, std::size_t Offset>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL extract(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    auto result = Value<Bytes> {};
    for (auto i = std::size_t {0}; i < Bytes; ++i) {
        const auto source = i + Offset;
        result.bytes[i] = source < Bytes ? a.bytes[source] : b.bytes[source - Bytes];
    }
    return result;
}

} // namespace backend
} // namespace scalar
} // namespace carven::runtime::simd
