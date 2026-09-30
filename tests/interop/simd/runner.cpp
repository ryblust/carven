#include "provider.hpp"

#include <carven/generated/carven-test-runner.hpp>
#include <carven/runtime/simd/simd.hpp>

#include <array>
#include <bit>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>
#include <type_traits>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/mman.h>
#include <unistd.h>

namespace {
constexpr auto site = carven::runtime::SourceSite::native();
} // namespace
#endif

namespace {

namespace rt = carven::runtime;
namespace simd = rt::simd;

template<typename... Value>
constexpr auto has_value_representation =
    ((std::is_aggregate_v<Value>
      && std::is_standard_layout_v<Value>
      && std::is_trivially_copyable_v<Value>)
     && ...);

static_assert(has_value_representation<
              simd::U8x16,
              simd::U8x32,
              simd::F32x4,
              simd::F32x8,
              simd::Mask16,
              simd::Mask32,
              simd::Mask4,
              simd::Mask8>);

template<typename Vector>
auto preserves_float_storage() noexcept -> bool {
    constexpr auto patterns = std::array {0x00000000u, 0x80000000u, 0x7fc12345u, 0xffc12345u};
    auto input = decltype(Vector {}.to_array()) {};
    for (auto i = std::size_t {0}; i < input.size(); ++i) {
        input[i] = std::bit_cast<float>(patterns[i % patterns.size()]);
    }
    const auto value = Vector::from_array(input);
    const auto output = value.to_array();
    for (auto i = std::size_t {0}; i < input.size(); ++i) {
        const auto expected = patterns[i % patterns.size()];
        if (std::bit_cast<std::uint32_t>(output[i]) != expected
            || std::bit_cast<std::uint32_t>(value.lane(i, site)) != expected) {
            return false;
        }
    }
    return true;
}

auto aborted(int signal) noexcept -> void {
    std::_Exit(signal == SIGABRT ? 73 : 74);
}

} // namespace

auto simd_float_storage() noexcept -> bool {
    return preserves_float_storage<simd::F32x4>() && preserves_float_storage<simd::F32x8>();
}

auto simd_guard_pages_available() noexcept -> bool {
#if defined(__unix__) || defined(__APPLE__)
    return true;
#else
    return false;
#endif
}

auto simd_guarded_tails() noexcept -> std::size_t {
#if defined(__unix__) || defined(__APPLE__)
    const auto page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) {
        return 1;
    }
    const auto page = static_cast<std::size_t>(page_size);
    auto* allocation =
        mmap(nullptr, page * 2, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (allocation == MAP_FAILED) {
        return 2;
    }
    auto* bytes = static_cast<std::uint8_t*>(allocation);
    if (mprotect(bytes + page, page, PROT_NONE) != 0) {
        static_cast<void>(munmap(allocation, page * 2));
        return 3;
    }
    auto status = std::size_t {0};
    for (auto count = std::size_t {0}; count <= 32; ++count) {
        auto* start = bytes + page - count;
        for (auto i = std::size_t {0}; i < count; ++i) {
            start[i] = static_cast<std::uint8_t>(i + 1);
        }
        const auto input = rt::Slice<std::uint8_t>(std::span<const std::uint8_t>(start, count));
        const auto narrow = simd::U8x16::load_partial(input, 0, 99, site).to_array();
        const auto wide = simd::U8x32::load_partial(input, 0, 99, site).to_array();
        for (auto i = std::size_t {0}; i < narrow.size(); ++i) {
            if (narrow[i] != (i < count ? i + 1 : 99)) {
                status = 4;
            }
        }
        for (auto i = std::size_t {0}; i < wide.size(); ++i) {
            if (wide[i] != (i < count ? i + 1 : 99)) {
                status = 5;
            }
        }
    }
    for (auto count = std::size_t {0}; count <= 8; ++count) {
        auto* start = reinterpret_cast<float*>(bytes + page - count * sizeof(float));
        for (auto i = std::size_t {0}; i < count; ++i) {
            start[i] = static_cast<float>(i + 1);
        }
        const auto input = rt::Slice<float>(std::span<const float>(start, count));
        const auto narrow = simd::F32x4::load_partial(input, 0, -1.0f, site).to_array();
        const auto wide = simd::F32x8::load_partial(input, 0, -1.0f, site).to_array();
        for (auto i = std::size_t {0}; i < narrow.size(); ++i) {
            if (narrow[i] != (i < count ? static_cast<float>(i + 1) : -1.0f)) {
                status = 6;
            }
        }
        for (auto i = std::size_t {0}; i < wide.size(); ++i) {
            if (wide[i] != (i < count ? static_cast<float>(i + 1) : -1.0f)) {
                status = 7;
            }
        }
    }
    if (munmap(allocation, page * 2) != 0) {
        return 8;
    }
    return status;
#else
    return 0;
#endif
}

// NOLINTNEXTLINE(misc-const-correctness): Keep the standard C++ main signature.
auto main(int argc, char** argv) noexcept -> int {
    if (argc == 1) {
        return rt::run_generated_tests();
    }
    if (argc != 2) {
        return 1;
    }
    std::signal(SIGABRT, aborted);
    const auto operation = std::string_view(argv[1]);
    if (operation == "wide-short-load") {
        const auto data = std::array<std::uint8_t, 31> {};
        static_cast<void>(simd::U8x32::load(rt::as_slice(data), 0, site));
    } else if (operation == "wide-prefix") {
        static_cast<void>(simd::Mask32::prefix(33, site));
    } else if (operation == "short-load") {
        const auto data = std::array<std::uint8_t, 15> {};
        static_cast<void>(simd::U8x16::load(rt::as_slice(data), 0, site));
    } else if (operation == "partial-offset") {
        static_cast<void>(simd::U8x16::load_partial(rt::Slice<std::uint8_t>(), 1, 0, site));
    } else if (operation == "lane") {
        static_cast<void>(simd::U8x16::splat(0).lane(16, site));
    } else if (operation == "mask-prefix") {
        static_cast<void>(simd::Mask16::prefix(17, site));
    } else if (operation == "float-short-load") {
        const auto data = std::array<float, 3> {};
        static_cast<void>(simd::F32x4::load(rt::as_slice(data), 0, site));
    } else if (operation == "float-partial-offset") {
        static_cast<void>(simd::F32x4::load_partial(rt::Slice<float>(), 1, 0.0f, site));
    } else if (operation == "float-lane") {
        static_cast<void>(simd::F32x4 {}.lane(4, site));
    } else if (operation == "float-update") {
        static_cast<void>(simd::F32x4 {}.with_lane(4, 1.0f, site));
    } else if (operation == "float-prefix") {
        static_cast<void>(simd::Mask4::prefix(5, site));
    }
    return 1;
}
