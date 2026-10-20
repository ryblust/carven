#include <carven/api/tests/interop/scalars/scalars.hpp>
#include <carven/api/tests/interop/scalars/scalars.hpp>

#include "scalars/providers.hpp"

#include <cstdlib>
#include <type_traits>

namespace {

namespace api = carven::api::tests::interop::scalars::scalars;

static_assert(
    std::is_same_v<decltype(api::echo_bool), auto(bool) noexcept -> carven::runtime::Outcome<bool>>
);

static_assert(std::is_same_v<
              decltype(api::echo_char),
              auto(char32_t) noexcept -> carven::runtime::Outcome<char32_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_i8),
              auto(std::int8_t) noexcept -> carven::runtime::Outcome<std::int8_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_i16),
              auto(std::int16_t) noexcept -> carven::runtime::Outcome<std::int16_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_i32),
              auto(std::int32_t) noexcept -> carven::runtime::Outcome<std::int32_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_i64),
              auto(std::int64_t) noexcept -> carven::runtime::Outcome<std::int64_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_u8),
              auto(std::uint8_t) noexcept -> carven::runtime::Outcome<std::uint8_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_u16),
              auto(std::uint16_t) noexcept -> carven::runtime::Outcome<std::uint16_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_u32),
              auto(std::uint32_t) noexcept -> carven::runtime::Outcome<std::uint32_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_u64),
              auto(std::uint64_t) noexcept -> carven::runtime::Outcome<std::uint64_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_isize),
              auto(std::ptrdiff_t) noexcept -> carven::runtime::Outcome<std::ptrdiff_t>>);

static_assert(std::is_same_v<
              decltype(api::echo_usize),
              auto(std::size_t) noexcept -> carven::runtime::Outcome<std::size_t>>);

static_assert(
    std::is_same_v<decltype(api::echo_f32), auto(float) noexcept -> carven::runtime::Outcome<float>>
);

static_assert(std::is_same_v<
              decltype(api::echo_f64),
              auto(double) noexcept -> carven::runtime::Outcome<double>>);

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
        const auto identity = api::invoke_identity(7);
        if (api::expression_scalar(4) != 5
            || !identity.success_if()
            || identity.success_if()->value != 7) {
            std::abort();
        }
        const auto result_bool = api::echo_bool(true);
        const auto result_char = api::echo_char(U'Z');
        const auto result_i8 = api::echo_i8(std::int8_t {-8});
        const auto result_i16 = api::echo_i16(std::int16_t {-16});
        const auto result_i32 = api::echo_i32(std::int32_t {-32});
        const auto result_i64 = api::echo_i64(std::int64_t {-64});
        const auto result_u8 = api::echo_u8(std::uint8_t {8});
        const auto result_u16 = api::echo_u16(std::uint16_t {16});
        const auto result_u32 = api::echo_u32(std::uint32_t {32});
        const auto result_u64 = api::echo_u64(std::uint64_t {64});
        const auto result_isize = api::echo_isize(std::ptrdiff_t {-7});
        const auto result_usize = api::echo_usize(std::size_t {7});
        const auto result_f32 = api::echo_f32(1.25f);
        const auto result_f64 = api::echo_f64(2.5);
        if (!result_bool.success_if()
            || result_bool.success_if()->value != true
            || !result_char.success_if()
            || result_char.success_if()->value != U'Z'
            || !result_i8.success_if()
            || result_i8.success_if()->value != std::int8_t {-8}
            || !result_i16.success_if()
            || result_i16.success_if()->value != std::int16_t {-16}
            || !result_i32.success_if()
            || result_i32.success_if()->value != std::int32_t {-32}
            || !result_i64.success_if()
            || result_i64.success_if()->value != std::int64_t {-64}
            || !result_u8.success_if()
            || result_u8.success_if()->value != std::uint8_t {8}
            || !result_u16.success_if()
            || result_u16.success_if()->value != std::uint16_t {16}
            || !result_u32.success_if()
            || result_u32.success_if()->value != std::uint32_t {32}
            || !result_u64.success_if()
            || result_u64.success_if()->value != std::uint64_t {64}
            || !result_isize.success_if()
            || result_isize.success_if()->value != std::ptrdiff_t {-7}
            || !result_usize.success_if()
            || result_usize.success_if()->value != std::size_t {7}
            || !result_f32.success_if()
            || result_f32.success_if()->value != 1.25f
            || !result_f64.success_if()
            || result_f64.success_if()->value != 2.5) {
            std::abort();
        }
    }
};

const VerifyCppAPI verify_cpp_api {};

} // namespace
