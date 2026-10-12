#include "scan.hpp"

namespace simd_benchmark {

namespace {

auto matches(std::uint8_t value) noexcept -> bool {
#if CARVEN_SIMD_BENCH_DIGITS
    return value >= 48 && value <= 57;
#elif CARVEN_SIMD_BENCH_FIELD
    return value != 10 && value != 13 && value != 34 && value != 44;
#else
    switch (value) {
        case 0:
        case 9:
        case 34:
        case 47:
        case 65:
        case 97:
        case 127:
        case 128:
        case 200:
        case 255: return true;
        default:  return false;
    }
#endif
}

} // namespace

auto scan(Bytes bytes, std::size_t start) noexcept -> std::size_t {
#if CARVEN_SIMD_BENCH_COUNT
    auto count = std::size_t {0};
    for (const auto value : bytes) {
        count += matches(value);
    }
    return count;
#else
    for (auto index = start; index < bytes.size(); ++index) {
#if CARVEN_SIMD_BENCH_FIND
        if (matches(bytes.data()[index])) {
#else
        if (!matches(bytes.data()[index])) {
#endif
            return index;
        }
    }
    return bytes.size();
#endif
}

} // namespace simd_benchmark
