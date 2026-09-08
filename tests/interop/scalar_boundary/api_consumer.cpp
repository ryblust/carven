#include "carven/api/tests/interop/scalar_boundary/scalars.hpp"
#include "carven/api/tests/interop/scalar_boundary/scalars.hpp"

#include "scalar_boundary/providers.hpp"

#include <cstdlib>
#include <type_traits>

namespace {

namespace api = carven::api::tests::interop::scalar_boundary::scalars;

static_assert(std::is_same_v<decltype(api::echo_bool), auto(bool) noexcept -> bool>);
static_assert(std::is_same_v<decltype(api::echo_char), auto(char32_t) noexcept -> char32_t>);
static_assert(std::is_same_v<decltype(api::echo_i8), auto(std::int8_t) noexcept -> std::int8_t>);
static_assert(std::is_same_v<decltype(api::echo_i16), auto(std::int16_t) noexcept -> std::int16_t>);
static_assert(std::is_same_v<decltype(api::echo_i32), auto(std::int32_t) noexcept -> std::int32_t>);
static_assert(std::is_same_v<decltype(api::echo_i64), auto(std::int64_t) noexcept -> std::int64_t>);
static_assert(std::is_same_v<decltype(api::echo_u8), auto(std::uint8_t) noexcept -> std::uint8_t>);
static_assert(
    std::is_same_v<decltype(api::echo_u16), auto(std::uint16_t) noexcept -> std::uint16_t>
);
static_assert(
    std::is_same_v<decltype(api::echo_u32), auto(std::uint32_t) noexcept -> std::uint32_t>
);
static_assert(
    std::is_same_v<decltype(api::echo_u64), auto(std::uint64_t) noexcept -> std::uint64_t>
);
static_assert(
    std::is_same_v<decltype(api::echo_isize), auto(std::ptrdiff_t) noexcept -> std::ptrdiff_t>
);
static_assert(std::is_same_v<decltype(api::echo_usize), auto(std::size_t) noexcept -> std::size_t>);
static_assert(std::is_same_v<decltype(api::echo_f32), auto(float) noexcept -> float>);
static_assert(std::is_same_v<decltype(api::echo_f64), auto(double) noexcept -> double>);

static_assert(
    std::is_same_v<decltype(api::expression_scalar), auto(std::int32_t) noexcept -> std::int32_t>
);
static_assert(std::is_same_v<decltype(api::expression_void), auto() noexcept -> void>);

struct VerifyCppAPI final {
    VerifyCppAPI() noexcept {
        scalar_boundary_detail::expression_calls = 0;
        api::expression_void();
        if (scalar_boundary_detail::expression_calls != 1) {
            std::abort();
        }
        if (api::expression_scalar(4) != 5) {
            std::abort();
        }
        if (!api::echo_bool(true)
            || api::echo_char(U'Z') != U'Z'
            || api::echo_i8(std::int8_t {-8}) != std::int8_t {-8}
            || api::echo_i16(std::int16_t {-16}) != std::int16_t {-16}
            || api::echo_i32(std::int32_t {-32}) != std::int32_t {-32}
            || api::echo_i64(std::int64_t {-64}) != std::int64_t {-64}
            || api::echo_u8(std::uint8_t {8}) != std::uint8_t {8}
            || api::echo_u16(std::uint16_t {16}) != std::uint16_t {16}
            || api::echo_u32(std::uint32_t {32}) != std::uint32_t {32}
            || api::echo_u64(std::uint64_t {64}) != std::uint64_t {64}
            || api::echo_isize(std::ptrdiff_t {-7}) != std::ptrdiff_t {-7}
            || api::echo_usize(std::size_t {7}) != std::size_t {7}
            || api::echo_f32(1.25f) != 1.25f
            || api::echo_f64(2.5) != 2.5) {
            std::abort();
        }
    }
};

const VerifyCppAPI verify_cpp_api {};

} // namespace
