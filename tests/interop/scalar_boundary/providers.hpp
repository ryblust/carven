#pragma once

#include <cstddef>
#include <cstdint>

namespace scalar_boundary_detail {

template<typename Type>
constexpr auto echo(Type value) noexcept -> Type {
    return value;
}

} // namespace scalar_boundary_detail

inline auto native_bool(bool value) noexcept -> bool {
    return scalar_boundary_detail::echo(value);
}

inline auto native_char(char32_t value) noexcept -> char32_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_i8(std::int8_t value) noexcept -> std::int8_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_i16(std::int16_t value) noexcept -> std::int16_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_i32(std::int32_t value) noexcept -> std::int32_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_i64(std::int64_t value) noexcept -> std::int64_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_u8(std::uint8_t value) noexcept -> std::uint8_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_u16(std::uint16_t value) noexcept -> std::uint16_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_u32(std::uint32_t value) noexcept -> std::uint32_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_u64(std::uint64_t value) noexcept -> std::uint64_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_isize(std::ptrdiff_t value) noexcept -> std::ptrdiff_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_usize(std::size_t value) noexcept -> std::size_t {
    return scalar_boundary_detail::echo(value);
}

inline auto native_f32(float value) noexcept -> float {
    return scalar_boundary_detail::echo(value);
}

inline auto native_f64(double value) noexcept -> double {
    return scalar_boundary_detail::echo(value);
}
