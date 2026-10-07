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
#include <type_traits>

namespace carven::runtime::simd {
inline namespace portable {
namespace backend {

// Float lanes keep their native object representation. Masks are canonical, so
// every byte of a true lane is 255 regardless of byte order.
template<std::size_t Bytes>
struct Storage final {
    std::array<std::uint8_t, Bytes> bytes;
};

#if defined(__clang__) || defined(__GNUC__)
template<typename Element>
struct CompilerVector;

template<>
struct CompilerVector<std::uint8_t> final {
    typedef std::uint8_t Type __attribute__((vector_size(16)));
};

template<>
struct CompilerVector<float> final {
    typedef float Type __attribute__((vector_size(16)));
};
#endif

template<typename Element, std::size_t Bytes, typename Operation>
CARVEN_SIMD_INLINE auto map(Storage<Bytes> a, Storage<Bytes> b, Operation operation) noexcept
    -> Storage<Bytes> {
    auto result = Storage<Bytes> {};
#if defined(__clang__) || defined(__GNUC__)
    using Vector = typename CompilerVector<Element>::Type;
    static_assert(Bytes % sizeof(Vector) == 0);
    for (auto offset = std::size_t {0}; offset < Bytes; offset += sizeof(Vector)) {
        auto left = Vector {};
        auto right = Vector {};
        std::memcpy(&left, a.bytes.data() + offset, sizeof(Vector));
        std::memcpy(&right, b.bytes.data() + offset, sizeof(Vector));
        auto value = operation(left, right);
        static_assert(sizeof(value) == sizeof(Vector));
        std::memcpy(result.bytes.data() + offset, &value, sizeof(Vector));
    }
#else
    for (auto offset = std::size_t {0}; offset < Bytes; offset += sizeof(Element)) {
        auto left = Element {};
        auto right = Element {};
        std::memcpy(&left, a.bytes.data() + offset, sizeof(Element));
        std::memcpy(&right, b.bytes.data() + offset, sizeof(Element));
        auto value = operation(left, right);
        if constexpr (std::is_same_v<decltype(value), bool>) {
            std::memset(result.bytes.data() + offset, value ? 255 : 0, sizeof(Element));
        } else {
            const auto lane = static_cast<Element>(value);
            std::memcpy(result.bytes.data() + offset, &lane, sizeof(Element));
        }
    }
#endif
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
    return map<std::uint8_t, Bytes>(a, b, [](auto a, auto b) noexcept { return a + b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sub_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<std::uint8_t, Bytes>(a, b, [](auto a, auto b) noexcept { return a - b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_and(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<std::uint8_t, Bytes>(a, b, [](auto a, auto b) noexcept { return a & b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_or(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<std::uint8_t, Bytes>(a, b, [](auto a, auto b) noexcept { return a | b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_xor(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<std::uint8_t, Bytes>(a, b, [](auto a, auto b) noexcept { return a ^ b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bit_not(Value<Bytes> a) noexcept -> Value<Bytes> {
    return bit_xor<Bytes>(a, splat_bytes<Bytes>(255));
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL equal_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<std::uint8_t, Bytes>(a, b, [](auto a, auto b) noexcept { return a == b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_bytes(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<std::uint8_t, Bytes>(a, b, [](auto a, auto b) noexcept { return a < b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL add_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<float, Bytes>(a, b, [](auto a, auto b) noexcept { return a + b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sub_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<float, Bytes>(a, b, [](auto a, auto b) noexcept { return a - b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL mul_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<float, Bytes>(a, b, [](auto a, auto b) noexcept { return a * b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL div_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<float, Bytes>(a, b, [](auto a, auto b) noexcept { return a / b; });
}

// Ordered comparisons: every comparison involving NaN is false.
template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL equal_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<float, Bytes>(a, b, [](auto a, auto b) noexcept { return a == b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<float, Bytes>(a, b, [](auto a, auto b) noexcept { return a < b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL less_equal_floats(Value<Bytes> a, Value<Bytes> b) noexcept
    -> Value<Bytes> {
    return map<float, Bytes>(a, b, [](auto a, auto b) noexcept { return a <= b; });
}

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
select(Value<Bytes> mask, Value<Bytes> yes, Value<Bytes> no) noexcept -> Value<Bytes> {
    return bit_or<Bytes>(bit_and<Bytes>(mask, yes), bit_and<Bytes>(bit_not<Bytes>(mask), no));
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
    return map<std::uint8_t, Bytes>(a, a, [](auto byte, auto) noexcept {
        const auto count = static_cast<std::uint8_t>(Count);
        return Left ? byte << count : byte >> count;
    });
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

template<std::size_t Bytes>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sum(Value<Bytes> a) noexcept -> std::size_t {
    auto result = std::uint32_t {0};
    for (const auto byte : a.bytes) {
        result += byte;
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
} // namespace portable
} // namespace carven::runtime::simd
