#include "scan.hpp"
#if CARVEN_SIMD_BENCH_CARVEN
#include "carven/api/workload.hpp"
#endif
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string_view>
#include <vector>

namespace {

auto argument(const char* name) noexcept -> std::size_t {
    const auto* value = std::getenv(name);
    if (value == nullptr) {
        std::abort();
    }
    return static_cast<std::size_t>(std::strtoull(value, nullptr, 10));
}

} // namespace

auto main() -> int {
    const auto length = argument("CARVEN_SIMD_BENCH_LENGTH");
    const auto iterations = argument("CARVEN_SIMD_BENCH_ITERATIONS");
    const auto distribution = std::string_view(std::getenv("CARVEN_SIMD_BENCH_DISTRIBUTION"));
    auto membership = std::array<bool, 256> {};
#if CARVEN_SIMD_BENCH_DIGITS
    for (auto value = 48; value <= 57; ++value) {
        membership[value] = true;
    }
    constexpr auto member = std::uint8_t {53};
    constexpr auto other = std::uint8_t {88};
#elif CARVEN_SIMD_BENCH_FIELD
    membership.fill(true);
    for (const auto value : {10, 13, 34, 44}) {
        membership[value] = false;
    }
    constexpr auto other = std::uint8_t {44};
#else
    for (const auto value : {0, 9, 34, 47, 65, 97, 127, 128, 200, 255}) {
        membership[value] = true;
    }
    constexpr auto member = std::uint8_t {200};
    constexpr auto other = std::uint8_t {88};
#endif
    // One byte of leading backing keeps every tested view unaligned.
    auto backing = std::vector<std::uint8_t>(length + 1);
    auto input = std::span(backing).subspan(1);
#if CARVEN_SIMD_BENCH_COUNT
    auto state = std::uint32_t {1729};
    for (auto& byte : input) {
        state = state * 1664525u + 1013904223u;
        byte = static_cast<std::uint8_t>(state >> 24);
    }
#else
#if CARVEN_SIMD_BENCH_FIELD
    constexpr auto terminal = other;
#elif CARVEN_SIMD_BENCH_FIND
    constexpr auto ordinary = other;
    constexpr auto terminal = member;
#else
    constexpr auto ordinary = member;
    constexpr auto terminal = other;
#endif
#if CARVEN_SIMD_BENCH_FIELD
    constexpr auto field = std::string_view("customer-2026@example.org");
    for (auto index = std::size_t {0}; index < length; ++index) {
        input[index] = static_cast<std::uint8_t>(field[index % field.size()]);
    }
#else
    for (auto& byte : input) {
        byte = ordinary;
    }
#endif
    if (distribution == "early") {
        input[1] = terminal;
    }
    if (distribution == "late") {
        input[length - 1] = terminal;
    }
#endif
    const auto bytes = simd_benchmark::Bytes(input);
    // Search starts at one, exercising absolute positions and another alignment.
    constexpr auto start = std::size_t {1};
    auto expected = std::size_t {0};
#if CARVEN_SIMD_BENCH_COUNT
    for (const auto byte : input) {
        expected += membership[byte];
    }
#else
    expected = length;
    for (auto index = start; index < length; ++index) {
#if CARVEN_SIMD_BENCH_FIND
        const auto stop = membership[input[index]];
#else
        const auto stop = !membership[input[index]];
#endif
        if (stop) {
            expected = index;
            break;
        }
    }
#endif
#if CARVEN_SIMD_BENCH_CARVEN
    constexpr auto kernel = &carven::api::workload::scan_kernel;
#else
    constexpr auto kernel = &simd_benchmark::scan;
#endif
    if (kernel(bytes, start) != expected) {
        return 2;
    }
    auto checksum = std::uint64_t {0};
    const auto begin = std::chrono::steady_clock::now();
    for (auto iteration = std::size_t {0}; iteration < iterations; ++iteration) {
        checksum += kernel(bytes, start);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - begin
    )
                             .count();
    if (checksum != expected * iterations) {
        return 3;
    }
#if CARVEN_SIMD_BENCH_CARVEN
#if !defined(CARVEN_SIMD_FORCE_PORTABLE) && defined(__aarch64__) && defined(__ARM_NEON)
    constexpr auto backend_field = R"(,"runtime_backend":"neon")";
#elif !defined(CARVEN_SIMD_FORCE_PORTABLE) && defined(__AVX2__)
    constexpr auto backend_field = R"(,"runtime_backend":"avx2")";
#else
    constexpr auto backend_field = R"(,"runtime_backend":"portable")";
#endif
#else
    constexpr auto backend_field = "";
#endif
    std::printf(
        "{\"elapsed_ns\":%lld,\"checksum\":%llu,\"expected\":%llu,\"iterations\":%llu%s}\n",
        static_cast<long long>(elapsed),
        static_cast<unsigned long long>(checksum),
        static_cast<unsigned long long>(expected),
        static_cast<unsigned long long>(iterations),
        backend_field
    );
}
