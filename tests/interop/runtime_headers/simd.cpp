#include <carven/runtime/simd/simd.hpp>

namespace {
constexpr auto site = carven::runtime::SourceSite::native();
} // namespace

auto simd_header_contract() noexcept -> bool {
    namespace simd = carven::runtime::simd;
    const auto bytes = std::array<std::uint8_t, 16> {};
    const auto value = simd::U8x16::load(carven::runtime::Slice<std::uint8_t>(bytes), 0, site);
    const auto selected = simd::Mask16::prefix(1, site).select(value, simd::U8x16::splat(7));
    const auto floats = simd::F32x8::splat(2.0f);
    const auto mask = floats > simd::F32x8::splat(1.0f);
    return value.with_lane<15>(9).lane<15>() == 9
        && floats.with_lane<7>(3.0f).lane<7>() == 3.0f
        && selected.to_array()[1] == 7
        && mask.bits() == 255
        && mask.select(floats, -floats).lane(7, site) == 2.0f;
}
