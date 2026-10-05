#pragma once

// AArch64 NEON realization of the backend contract. Include simd.hpp instead.
#if !defined(CARVEN_SIMD_INLINE)
#error "include carven/runtime/simd/simd.hpp instead of a SIMD backend header"
#endif

#include <arm_neon.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace carven::runtime::simd {
inline namespace neon {
namespace backend {

// Wide logical vectors use two 128-bit registers.
template<std::size_t Bytes>
struct Storage;

template<>
struct Storage<16> final {
    using Value = uint8x16_t;
};

template<>
struct Storage<32> final {
    struct Value final {
        uint8x16_t low;
        uint8x16_t high;
    };
};

template<std::size_t Bytes, typename Operation>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
map(typename Storage<Bytes>::Value a, Operation operation) noexcept ->
    typename Storage<Bytes>::Value {
    if constexpr (Bytes == 16) {
        return operation(a);
    } else {
        return {.low = operation(a.low), .high = operation(a.high)};
    }
}

template<std::size_t Bytes, typename Operation>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
map(typename Storage<Bytes>::Value a,
    typename Storage<Bytes>::Value b,
    Operation operation) noexcept -> typename Storage<Bytes>::Value {
    if constexpr (Bytes == 16) {
        return operation(a, b);
    } else {
        return {.low = operation(a.low, b.low), .high = operation(a.high, b.high)};
    }
}

template<std::size_t Bytes, typename Operation>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL map_floats(
    typename Storage<Bytes>::Value a,
    typename Storage<Bytes>::Value b,
    Operation operation
) noexcept -> typename Storage<Bytes>::Value {
    return map<Bytes>(a, b, [operation](uint8x16_t a, uint8x16_t b) noexcept {
        return vreinterpretq_u8_f32(operation(vreinterpretq_f32_u8(a), vreinterpretq_f32_u8(b)));
    });
}

template<std::size_t Bytes, typename Operation>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL compare_floats(
    typename Storage<Bytes>::Value a,
    typename Storage<Bytes>::Value b,
    Operation operation
) noexcept -> typename Storage<Bytes>::Value {
    return map<Bytes>(a, b, [operation](uint8x16_t a, uint8x16_t b) noexcept {
        return vreinterpretq_u8_u32(operation(vreinterpretq_f32_u8(a), vreinterpretq_f32_u8(b)));
    });
}

template<std::size_t Bytes>
using Value = typename Storage<Bytes>::Value;

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL splat_bytes(std::uint8_t element) noexcept
    -> Value<Bytes> {
    const auto chunk = vdupq_n_u8(element);
    if constexpr (Bytes == 16) {
        return chunk;
    } else {
        return {.low = chunk, .high = chunk};
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL splat_floats(float element) noexcept -> Value<Bytes> {
    const auto chunk = vreinterpretq_u8_f32(vdupq_n_f32(element));
    if constexpr (Bytes == 16) {
        return chunk;
    } else {
        return {.low = chunk, .high = chunk};
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL load(const void* data) noexcept -> Value<Bytes> {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if constexpr (Bytes == 16) {
        return vld1q_u8(bytes);
    } else {
        return {.low = vld1q_u8(bytes), .high = vld1q_u8(bytes + 16)};
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL store(void* data, Value<Bytes> value) noexcept -> void {
    auto* bytes = static_cast<std::uint8_t*>(data);
    if constexpr (Bytes == 16) {
        vst1q_u8(bytes, value);
    } else {
        vst1q_u8(bytes, value.low);
        vst1q_u8(bytes + 16, value.high);
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL add_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](auto a, auto b) noexcept { return vaddq_u8(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sub_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](auto a, auto b) noexcept { return vsubq_u8(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_and(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](auto a, auto b) noexcept { return vandq_u8(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_or(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](auto a, auto b) noexcept { return vorrq_u8(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_xor(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](auto a, auto b) noexcept { return veorq_u8(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_not(Value<Bytes> a) noexcept -> Value<Bytes> {
    return map<Bytes>(a, [](auto a) noexcept { return vmvnq_u8(a); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL equal_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](auto a, auto b) noexcept { return vceqq_u8(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<Bytes>(a, b, [](auto a, auto b) noexcept { return vcltq_u8(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL add_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map_floats<Bytes>(a, b, [](auto a, auto b) noexcept { return vaddq_f32(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sub_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map_floats<Bytes>(a, b, [](auto a, auto b) noexcept { return vsubq_f32(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL mul_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map_floats<Bytes>(a, b, [](auto a, auto b) noexcept { return vmulq_f32(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL div_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map_floats<Bytes>(a, b, [](auto a, auto b) noexcept { return vdivq_f32(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL equal_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<Bytes>(a, b, [](auto a, auto b) noexcept { return vceqq_f32(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<Bytes>(a, b, [](auto a, auto b) noexcept { return vcltq_f32(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_equal_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<Bytes>(a, b, [](auto a, auto b) noexcept { return vcleq_f32(a, b); });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
select(Value<Bytes> mask, Value<Bytes> yes, Value<Bytes> no) noexcept -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return vbslq_u8(mask, yes, no);
    } else {
        return {
            .low = vbslq_u8(mask.low, yes.low, no.low),
            .high = vbslq_u8(mask.high, yes.high, no.high)
        };
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL lookup(Value<Bytes> table, Value<Bytes> indices) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return vqtbl1q_u8(table, indices);
    } else {
        const auto pair = uint8x16x2_t {{table.low, table.high}};
        return {.low = vqtbl2q_u8(pair, indices.low), .high = vqtbl2q_u8(pair, indices.high)};
    }
}

template<std::size_t Bytes, std::size_t Count, bool Left>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL shift(Value<Bytes> a) noexcept -> Value<Bytes> {
    static_assert(Count < 8);
    if constexpr (Count == 0) {
        return a;
    } else {
        return map<Bytes>(a, [](auto chunk) noexcept {
            if constexpr (Left) {
                return vshlq_n_u8(chunk, Count);
            } else {
                return vshrq_n_u8(chunk, Count);
            }
        });
    }
}

template<std::size_t Bytes, bool Float>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bits(Value<Bytes> a) noexcept -> std::uint32_t {
    const auto chunk_bits = [](uint8x16_t chunk) noexcept -> std::uint32_t {
        if constexpr (Float) {
            const auto weights = std::array<std::uint32_t, 4> {1, 2, 4, 8};
            return vaddvq_u32(vandq_u32(vreinterpretq_u32_u8(chunk), vld1q_u32(weights.data())));
        } else {
            const auto weights = std::array<std::uint8_t, 8> {1, 2, 4, 8, 16, 32, 64, 128};
            const auto weight = vld1_u8(weights.data());
            const auto low = vaddv_u8(vand_u8(vget_low_u8(chunk), weight));
            const auto high = vaddv_u8(vand_u8(vget_high_u8(chunk), weight));
            return low | (static_cast<std::uint32_t>(high) << 8u);
        }
    };
    if constexpr (Bytes == 16) {
        return chunk_bits(a);
    } else {
        return chunk_bits(a.low) | (chunk_bits(a.high) << (Float ? 4u : 16u));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL any(Value<Bytes> a) noexcept -> bool {
    if constexpr (Bytes == 16) {
        return vmaxvq_u8(a) != 0;
    } else {
        return vmaxvq_u8(vorrq_u8(a.low, a.high)) != 0;
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL all(Value<Bytes> a) noexcept -> bool {
    if constexpr (Bytes == 16) {
        return vminvq_u8(a) == 255;
    } else {
        return vminvq_u8(vandq_u8(a.low, a.high)) == 255;
    }
}

template<std::size_t Bytes, bool Float>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL count(Value<Bytes> a) noexcept -> std::size_t {
    // Widen narrow sums and combine wide halves before reducing to avoid mask packing.
    if constexpr (Float) {
        const auto lanes = [](uint8x16_t chunk) noexcept {
            return vshrq_n_u32(vreinterpretq_u32_u8(chunk), 31);
        };
        if constexpr (Bytes == 16) {
            return vaddlvq_u32(lanes(a));
        } else {
            return vaddvq_u32(vaddq_u32(lanes(a.low), lanes(a.high)));
        }
    } else {
        const auto lanes = [](uint8x16_t chunk) noexcept {
            return vshrq_n_u8(chunk, 7);
        };
        if constexpr (Bytes == 16) {
            return vaddlvq_u8(lanes(a));
        } else {
            return vaddvq_u8(vaddq_u8(lanes(a.low), lanes(a.high)));
        }
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sum(Value<Bytes> a) noexcept -> std::size_t {
    if constexpr (Bytes == 16) {
        return vaddlvq_u8(a);
    } else {
        return vaddvq_u16(vaddq_u16(vpaddlq_u8(a.low), vpaddlq_u8(a.high)));
    }
}

// Offset is a byte offset in 1..Bytes-1; the public layer handles both ends.
template<std::size_t Bytes, std::size_t Offset>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL extract(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return vextq_u8(a, b, Offset);
    } else if constexpr (Offset < 16) {
        return {.low = vextq_u8(a.low, a.high, Offset), .high = vextq_u8(a.high, b.low, Offset)};
    } else if constexpr (Offset == 16) {
        return {.low = a.high, .high = b.low};
    } else {
        return {
            .low = vextq_u8(a.high, b.low, Offset - 16),
            .high = vextq_u8(b.low, b.high, Offset - 16)
        };
    }
}

} // namespace backend
} // namespace neon
} // namespace carven::runtime::simd
