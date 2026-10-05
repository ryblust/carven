#pragma once

// Native realization of Carven's builtin SIMD operations through one selected
// backend. Translation units sharing these types select the same backend. Inline
// namespaces give backend-specific types different identities; symbols that
// encode those types can diagnose some mismatches at link time.
// CARVEN_SIMD_FORCE_PORTABLE selects the portable backend for testing.

#include "../slice.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(_MSC_VER)
#define CARVEN_SIMD_INLINE __forceinline
#else
#define CARVEN_SIMD_INLINE inline __attribute__((always_inline))
#endif

#if defined(_WIN32)                                                                                \
    && (defined(_M_IX86) || defined(_M_X64) || defined(__i386__) || defined(__x86_64__))
#if defined(_MSC_VER)
#define CARVEN_SIMD_CALL __vectorcall
#elif defined(__clang__)
#define CARVEN_SIMD_CALL __attribute__((vectorcall))
#else
#define CARVEN_SIMD_CALL
#endif
#else
#define CARVEN_SIMD_CALL
#endif

#if !defined(CARVEN_SIMD_FORCE_PORTABLE) && defined(__aarch64__) && defined(__ARM_NEON)
#include "neon.hpp"
#define CARVEN_SIMD_ABI neon
#define CARVEN_SIMD_NATIVE_BACKEND true
#elif !defined(CARVEN_SIMD_FORCE_PORTABLE) && defined(__AVX2__)
#include "avx2.hpp"
#define CARVEN_SIMD_ABI avx2
#define CARVEN_SIMD_NATIVE_BACKEND true
#else
#include "portable.hpp"
#define CARVEN_SIMD_ABI portable
#define CARVEN_SIMD_NATIVE_BACKEND false
#endif

namespace carven::runtime::simd {
inline namespace CARVEN_SIMD_ABI {

inline constexpr auto uses_native_backend = CARVEN_SIMD_NATIVE_BACKEND;

template<typename Element, std::size_t Lanes>
struct Vector final {
    static_assert(
        (std::is_same_v<Element, std::uint8_t> && (Lanes == 16 || Lanes == 32))
            || (std::is_same_v<Element, float> && (Lanes == 4 || Lanes == 8)),
        "Carven SIMD vectors support only u8x16, u8x32, f32x4 and f32x8"
    );

    using Value = backend::Value<sizeof(Element) * Lanes>;

    static CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL splat(Element element) noexcept -> Vector {
        if constexpr (std::is_same_v<Element, float>) {
            return Vector {.value = backend::splat_floats<sizeof(Element) * Lanes>(element)};
        } else {
            return Vector {.value = backend::splat_bytes<sizeof(Element) * Lanes>(element)};
        }
    }

    static CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
    from_array(const std::array<Element, Lanes>& data) noexcept -> Vector {
        return Vector {.value = backend::load<sizeof(Element) * Lanes>(data.data())};
    }

    template<typename... Lane>
    static CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL from_lanes(Lane... lane) noexcept -> Vector {
        static_assert(sizeof...(Lane) == Lanes);
        return from_array({static_cast<Element>(lane)...});
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL to_array() const noexcept
        -> std::array<Element, Lanes> {
        auto data = std::array<Element, Lanes>();
        backend::store<sizeof(Element) * Lanes>(data.data(), value);
        return data;
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL sum() const noexcept -> std::size_t
        requires std::is_same_v<Element, std::uint8_t>
    {
        return backend::sum<sizeof(Element) * Lanes>(value);
    }

    static CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
    load(Slice<Element> data, std::size_t offset, SourceSite site) noexcept -> Vector {
        if (offset > data.size() || data.size() - offset < Lanes) {
            trap("SIMD index or memory range is out of bounds", site);
        }
        return Vector {.value = backend::load<sizeof(Element) * Lanes>(data.data() + offset)};
    }

    static CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
    load_partial(Slice<Element> data, std::size_t offset, Element fill, SourceSite site) noexcept
        -> Vector {
        if (offset > data.size()) {
            trap("SIMD index or memory range is out of bounds", site);
        }
        const auto count = data.size() - offset;
        if (count >= Lanes) {
            return load(data, offset, site);
        }
        auto buffer = std::array<Element, Lanes>();
        buffer.fill(fill);
        // Copy exactly the readable suffix, including zero bytes at a page end.
        if (count != 0) {
            std::memcpy(buffer.data(), data.data() + offset, count * sizeof(Element));
        }
        return from_array(buffer);
    }

    // Generated calls use these overloads only after proving the control in range.
    // Native callers receive the same bound as a compile-time contract.
    template<std::size_t Index>
    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL lane() const noexcept -> Element {
        static_assert(Index < Lanes);
        return to_array()[Index];
    }

    template<std::size_t Index>
    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL with_lane(Element element) const noexcept -> Vector {
        static_assert(Index < Lanes);
        auto data = to_array();
        data[Index] = element;
        return from_array(data);
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL lane(std::size_t index, SourceSite site) const noexcept
        -> Element {
        if (index >= Lanes) {
            trap("SIMD index or memory range is out of bounds", site);
        }
        return to_array()[index];
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
    with_lane(std::size_t index, Element element, SourceSite site) const noexcept -> Vector {
        if (index >= Lanes) {
            trap("SIMD index or memory range is out of bounds", site);
        }
        auto data = to_array();
        data[index] = element;
        return from_array(data);
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL lookup(Vector indices) const noexcept -> Vector
        requires std::is_same_v<Element, std::uint8_t>
    {
        return Vector {.value = backend::lookup<sizeof(Element) * Lanes>(value, indices.value)};
    }

    template<std::size_t Offset>
    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL extract(Vector next) const noexcept -> Vector {
        static_assert(Offset <= Lanes);
        if constexpr (Offset == 0) {
            return *this;
        } else if constexpr (Offset == Lanes) {
            return next;
        } else {
            return Vector {
                .value = backend::extract<sizeof(Element) * Lanes, Offset * sizeof(Element)>(
                    value,
                    next.value
                )
            };
        }
    }

    template<std::size_t Count>
    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL shift_left() const noexcept -> Vector
        requires std::is_same_v<Element, std::uint8_t>
    {
        static_assert(Count < 8);
        return Vector {.value = backend::shift<sizeof(Element) * Lanes, Count, true>(value)};
    }

    template<std::size_t Count>
    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL shift_right() const noexcept -> Vector
        requires std::is_same_v<Element, std::uint8_t>
    {
        static_assert(Count < 8);
        return Vector {.value = backend::shift<sizeof(Element) * Lanes, Count, false>(value)};
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL native() const noexcept -> Value { return value; }

    // A transparent aggregate keeps vectorcall returns in SIMD registers for
    // both MSVC and clang-cl. Default zero storage preserves value construction.
    Value value {};
};

template<typename Element, std::size_t Lanes>
struct Mask final {
    using VectorType = Vector<Element, Lanes>;
    using Value = typename VectorType::Value;
    using Bits = std::conditional_t<
        (Lanes <= 8),
        std::uint8_t,
        std::conditional_t<(Lanes <= 16), std::uint16_t, std::uint32_t>>;

    static CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL from_bits(Bits bits) noexcept -> Mask {
        auto bytes = std::array<std::uint8_t, sizeof(Element) * Lanes>();
        for (std::size_t i = 0; i < Lanes; ++i) {
            for (std::size_t j = 0; j < sizeof(Element); ++j) {
                bytes[i * sizeof(Element) + j] =
                    ((static_cast<std::uint32_t>(bits) >> i) & 1u) != 0 ? 255 : 0;
            }
        }
        return Mask {.value = backend::load<sizeof(Element) * Lanes>(bytes.data())};
    }

    static CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL
    prefix(std::size_t count, SourceSite site) noexcept -> Mask {
        if (count > Lanes) {
            trap("SIMD index or memory range is out of bounds", site);
        }
        return from_bits(static_cast<Bits>((std::uint64_t {1} << count) - 1u));
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL bits() const noexcept -> Bits {
        return static_cast<Bits>(
            backend::bits<sizeof(Element) * Lanes, std::is_same_v<Element, float>>(value)
        );
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL any() const noexcept -> bool {
        return backend::any<sizeof(Element) * Lanes>(value);
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL all() const noexcept -> bool {
        return backend::all<sizeof(Element) * Lanes>(value);
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL count() const noexcept -> std::size_t {
        return backend::count<sizeof(Element) * Lanes, std::is_same_v<Element, float>>(value);
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL first_or(std::size_t fallback) const noexcept
        -> std::size_t {
        const auto packed = bits();
        return packed == 0 ? fallback : static_cast<std::size_t>(std::countr_zero(packed));
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL select(VectorType yes, VectorType no) const noexcept
        -> VectorType {
        return VectorType {
            .value = backend::select<sizeof(Element) * Lanes>(value, yes.native(), no.native())
        };
    }

    CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL native() const noexcept -> Value { return value; }

    // Native mask storage requires each lane to be zero or all bits set.
    // A transparent aggregate keeps vectorcall returns in SIMD registers for
    // both MSVC and clang-cl. Default zero storage preserves value construction.
    Value value {};
};

using U8x16 = Vector<std::uint8_t, 16>;
using U8x32 = Vector<std::uint8_t, 32>;
using F32x4 = Vector<float, 4>;
using F32x8 = Vector<float, 8>;
using Mask16 = Mask<std::uint8_t, 16>;
using Mask32 = Mask<std::uint8_t, 32>;
using Mask4 = Mask<float, 4>;
using Mask8 = Mask<float, 8>;

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator+(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Vector<T, N> {
    if constexpr (std::is_same_v<T, float>) {
        return Vector<T, N> {.value = backend::add_floats<sizeof(T) * N>(a.native(), b.native())};
    } else {
        return Vector<T, N> {.value = backend::add_bytes<sizeof(T) * N>(a.native(), b.native())};
    }
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator-(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Vector<T, N> {
    if constexpr (std::is_same_v<T, float>) {
        return Vector<T, N> {.value = backend::sub_floats<sizeof(T) * N>(a.native(), b.native())};
    } else {
        return Vector<T, N> {.value = backend::sub_bytes<sizeof(T) * N>(a.native(), b.native())};
    }
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator*(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Vector<T, N>
    requires std::is_same_v<T, float>
{
    return Vector<T, N> {.value = backend::mul_floats<sizeof(T) * N>(a.native(), b.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator/(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Vector<T, N>
    requires std::is_same_v<T, float>
{
    return Vector<T, N> {.value = backend::div_floats<sizeof(T) * N>(a.native(), b.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator&(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Vector<T, N>
    requires std::is_same_v<T, std::uint8_t>
{
    return Vector<T, N> {.value = backend::bit_and<sizeof(T) * N>(a.native(), b.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator|(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Vector<T, N>
    requires std::is_same_v<T, std::uint8_t>
{
    return Vector<T, N> {.value = backend::bit_or<sizeof(T) * N>(a.native(), b.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator^(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Vector<T, N>
    requires std::is_same_v<T, std::uint8_t>
{
    return Vector<T, N> {.value = backend::bit_xor<sizeof(T) * N>(a.native(), b.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator==(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Mask<T, N> {
    using MaskType = Mask<T, N>;
    if constexpr (std::is_same_v<T, float>) {
        return MaskType {.value = backend::equal_floats<sizeof(T) * N>(a.native(), b.native())};
    } else {
        return MaskType {.value = backend::equal_bytes<sizeof(T) * N>(a.native(), b.native())};
    }
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator!=(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Mask<T, N> {
    return ~(a == b);
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator<(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Mask<T, N> {
    using MaskType = Mask<T, N>;
    if constexpr (std::is_same_v<T, float>) {
        return MaskType {.value = backend::less_floats<sizeof(T) * N>(a.native(), b.native())};
    } else {
        return MaskType {.value = backend::less_bytes<sizeof(T) * N>(a.native(), b.native())};
    }
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator<=(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Mask<T, N> {
    using MaskType = Mask<T, N>;
    if constexpr (std::is_same_v<T, float>) {
        return MaskType {
            .value = backend::less_equal_floats<sizeof(T) * N>(a.native(), b.native())
        };
    } else {
        return ~(b < a);
    }
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator>(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Mask<T, N> {
    return b < a;
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator>=(Vector<T, N> a, Vector<T, N> b) noexcept
    -> Mask<T, N> {
    return b <= a;
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator~(Vector<T, N> a) noexcept -> Vector<T, N>
    requires std::is_same_v<T, std::uint8_t>
{
    return Vector<T, N> {.value = backend::bit_not<sizeof(T) * N>(a.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator-(Vector<T, N> a) noexcept -> Vector<T, N>
    requires std::is_same_v<T, float>
{
    // Toggle the sign bit, preserving signed zero and NaN payload bits.
    auto signs = std::array<std::uint32_t, N>();
    signs.fill(0x80000000u);
    return Vector<T, N> {
        .value =
            backend::bit_xor<sizeof(T) * N>(a.native(), backend::load<sizeof(T) * N>(signs.data()))
    };
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator~(Mask<T, N> a) noexcept -> Mask<T, N> {
    return Mask<T, N> {.value = backend::bit_not<sizeof(T) * N>(a.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator&(Mask<T, N> a, Mask<T, N> b) noexcept
    -> Mask<T, N> {
    return Mask<T, N> {.value = backend::bit_and<sizeof(T) * N>(a.native(), b.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator|(Mask<T, N> a, Mask<T, N> b) noexcept
    -> Mask<T, N> {
    return Mask<T, N> {.value = backend::bit_or<sizeof(T) * N>(a.native(), b.native())};
}

template<typename T, std::size_t N>
CARVEN_SIMD_INLINE auto CARVEN_SIMD_CALL operator^(Mask<T, N> a, Mask<T, N> b) noexcept
    -> Mask<T, N> {
    return Mask<T, N> {.value = backend::bit_xor<sizeof(T) * N>(a.native(), b.native())};
}

} // namespace CARVEN_SIMD_ABI
} // namespace carven::runtime::simd

#undef CARVEN_SIMD_NATIVE_BACKEND
#undef CARVEN_SIMD_ABI
#undef CARVEN_SIMD_CALL
#undef CARVEN_SIMD_INLINE
