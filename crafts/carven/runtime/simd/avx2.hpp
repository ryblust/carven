#pragma once

// x86 AVX2 realization of the backend contract. Include simd.hpp instead.
#if !defined(CARVEN_SIMD_INLINE)
#error "include carven/runtime/simd/simd.hpp instead of a SIMD backend header"
#endif

#include <bit>
#include <cstddef>
#include <cstdint>

#include <immintrin.h>

namespace carven::runtime::simd::inline avx2::backend {

template<std::size_t Bytes>
struct Storage;

template<>
struct Storage<16> final {
    using Value = __m128i;
};

template<>
struct Storage<32> final {
    using Value = __m256i;
};

template<std::size_t Bytes>
using Value = typename Storage<Bytes>::Value;

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL splat_bytes(std::uint8_t element) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_set1_epi8(static_cast<char>(element));
    } else {
        return _mm256_set1_epi8(static_cast<char>(element));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL splat_floats(float element) noexcept -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_castps_si128(_mm_set1_ps(element));
    } else {
        return _mm256_castps_si256(_mm256_set1_ps(element));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL load(const void* data) noexcept -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_loadu_si128(static_cast<const __m128i*>(data));
    } else {
        return _mm256_loadu_si256(static_cast<const __m256i*>(data));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL store(void* data, Value<Bytes> value) noexcept -> void {
    if constexpr (Bytes == 16) {
        _mm_storeu_si128(static_cast<__m128i*>(data), value);
    } else {
        _mm256_storeu_si256(static_cast<__m256i*>(data), value);
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL add_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_add_epi8(a, b);
    } else {
        return _mm256_add_epi8(a, b);
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sub_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_sub_epi8(a, b);
    } else {
        return _mm256_sub_epi8(a, b);
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_and(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_and_si128(a, b);
    } else {
        return _mm256_and_si256(a, b);
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_or(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_or_si128(a, b);
    } else {
        return _mm256_or_si256(a, b);
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_xor(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_xor_si128(a, b);
    } else {
        return _mm256_xor_si256(a, b);
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_not(Value<Bytes> a) noexcept -> Value<Bytes> {
    return bit_xor<Bytes>(a, splat_bytes<Bytes>(255));
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL equal_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_cmpeq_epi8(a, b);
    } else {
        return _mm256_cmpeq_epi8(a, b);
    }
}

// Unsigned ordering flips the sign bit before the signed comparison.
template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    const auto sign = splat_bytes<Bytes>(0x80);
    if constexpr (Bytes == 16) {
        return _mm_cmpgt_epi8(_mm_xor_si128(b, sign), _mm_xor_si128(a, sign));
    } else {
        return _mm256_cmpgt_epi8(_mm256_xor_si256(b, sign), _mm256_xor_si256(a, sign));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL add_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_castps_si128(_mm_add_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b)));
    } else {
        return _mm256_castps_si256(_mm256_add_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sub_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_castps_si128(_mm_sub_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b)));
    } else {
        return _mm256_castps_si256(_mm256_sub_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL mul_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_castps_si128(_mm_mul_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b)));
    } else {
        return _mm256_castps_si256(_mm256_mul_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL div_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_castps_si128(_mm_div_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b)));
    } else {
        return _mm256_castps_si256(_mm256_div_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b)));
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sqrt_floats(Value<Bytes> value) noexcept -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_castps_si128(_mm_sqrt_ps(_mm_castsi128_ps(value)));
    } else {
        return _mm256_castps_si256(_mm256_sqrt_ps(_mm256_castsi256_ps(value)));
    }
}

template<int Predicate, std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL compare_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_castps_si128(_mm_cmp_ps(_mm_castsi128_ps(a), _mm_castsi128_ps(b), Predicate));
    } else {
        return _mm256_castps_si256(
            _mm256_cmp_ps(_mm256_castsi256_ps(a), _mm256_castsi256_ps(b), Predicate)
        );
    }
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL equal_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<_CMP_EQ_OQ, Bytes>(a, b);
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<_CMP_LT_OQ, Bytes>(a, b);
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_equal_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return compare_floats<_CMP_LE_OQ, Bytes>(a, b);
}

// Mask lanes are canonical, so the byte sign bit is sufficient for blending.
template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
select(Value<Bytes> mask, Value<Bytes> yes, Value<Bytes> no) noexcept -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_blendv_epi8(no, yes, mask);
    } else {
        return _mm256_blendv_epi8(no, yes, mask);
    }
}

template<std::size_t TableBytes, std::size_t IndexBytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
lookup(Value<TableBytes> table, Value<IndexBytes> indices) noexcept -> Value<IndexBytes> {
    // Each 128-bit output half addresses the same table.
    const auto shuffle = [indices](__m128i part) noexcept -> Value<IndexBytes> {
        if constexpr (IndexBytes == 16) {
            return _mm_shuffle_epi8(part, indices);
        } else {
            return _mm256_shuffle_epi8(_mm256_broadcastsi128_si256(part), indices);
        }
    };
    const auto low = [&]() noexcept {
        if constexpr (TableBytes == 16) {
            return table;
        } else {
            return _mm256_castsi256_si128(table);
        }
    }();
    auto selected = shuffle(low);
    if constexpr (TableBytes == 32) {
        const auto upper = equal_bytes<IndexBytes>(
            bit_and<IndexBytes>(indices, splat_bytes<IndexBytes>(16)),
            splat_bytes<IndexBytes>(16)
        );
        selected = select<IndexBytes>(upper, shuffle(_mm256_extracti128_si256(table, 1)), selected);
    }
    const auto valid = equal_bytes<IndexBytes>(
        bit_and<IndexBytes>(
            indices,
            splat_bytes<IndexBytes>(static_cast<std::uint8_t>(-TableBytes))
        ),
        splat_bytes<IndexBytes>(0)
    );
    return bit_and<IndexBytes>(selected, valid);
}

// Word shifts need a per-byte mask to remove bits crossing byte lanes.
template<std::size_t Bytes, std::size_t Count, bool Left>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL shift(Value<Bytes> a) noexcept -> Value<Bytes> {
    static_assert(Count < 8);
    const auto mask =
        splat_bytes<Bytes>(static_cast<std::uint8_t>(Left ? (255u << Count) : (255u >> Count)));
    if constexpr (Bytes == 16) {
        return _mm_and_si128(Left ? _mm_slli_epi16(a, Count) : _mm_srli_epi16(a, Count), mask);
    } else {
        return _mm256_and_si256(
            Left ? _mm256_slli_epi16(a, Count) : _mm256_srli_epi16(a, Count),
            mask
        );
    }
}

template<std::size_t Bytes, bool Float>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bits(Value<Bytes> a) noexcept -> std::uint32_t {
    if constexpr (Float && Bytes == 16) {
        return static_cast<std::uint32_t>(_mm_movemask_ps(_mm_castsi128_ps(a)));
    } else if constexpr (Float) {
        return static_cast<std::uint32_t>(_mm256_movemask_ps(_mm256_castsi256_ps(a)));
    } else if constexpr (Bytes == 16) {
        return static_cast<std::uint32_t>(_mm_movemask_epi8(a));
    } else {
        return static_cast<std::uint32_t>(_mm256_movemask_epi8(a));
    }
}

template<std::size_t Bytes, bool Float>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL first_or(Value<Bytes> a, std::size_t fallback) noexcept
    -> std::size_t {
    const auto packed = bits<Bytes, Float>(a);
    return packed == 0 ? fallback : static_cast<std::size_t>(std::countr_zero(packed));
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL any(Value<Bytes> a) noexcept -> bool {
    return bits<Bytes, false>(a) != 0;
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL all(Value<Bytes> a) noexcept -> bool {
    return bits<Bytes, false>(a) == (Bytes == 16 ? 0xffffu : 0xffffffffu);
}

template<std::size_t Bytes, bool Float>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL count(Value<Bytes> a) noexcept -> std::size_t {
    return static_cast<std::size_t>(std::popcount(bits<Bytes, Float>(a)));
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sum(Value<Bytes> a) noexcept -> std::size_t {
    auto partial = _mm_setzero_si128();
    if constexpr (Bytes == 16) {
        partial = _mm_sad_epu8(a, _mm_setzero_si128());
    } else {
        const auto sums = _mm256_sad_epu8(a, _mm256_setzero_si256());
        partial = _mm_add_epi64(_mm256_castsi256_si128(sums), _mm256_extracti128_si256(sums, 1));
    }
    return static_cast<std::size_t>(
        _mm_cvtsi128_si32(_mm_add_epi64(partial, _mm_srli_si128(partial, 8)))
    );
}

// Offset is a byte offset in 1..Bytes-1; the public layer handles both ends.
template<std::size_t Bytes, std::size_t Offset>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL extract(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    if constexpr (Bytes == 16) {
        return _mm_alignr_epi8(b, a, Offset);
    } else if constexpr (Offset < 16) {
        const auto next = _mm256_permute2x128_si256(a, b, 0x21);
        return _mm256_alignr_epi8(next, a, Offset);
    } else if constexpr (Offset == 16) {
        return _mm256_permute2x128_si256(a, b, 0x21);
    } else {
        const auto current = _mm256_permute2x128_si256(a, b, 0x21);
        return _mm256_alignr_epi8(b, current, Offset - 16);
    }
}

} // namespace carven::runtime::simd::avx2::backend
