#include <carven/runtime/collection/slice.hpp>
#include <carven/api/workload.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <span>
#include <vector>

auto main() -> int {
    const auto* path = std::getenv("CARVEN_JSON_BENCH_INPUT");
    const auto* count = std::getenv("CARVEN_JSON_BENCH_ITERATIONS");
    const auto* result = std::getenv("CARVEN_JSON_BENCH_EXPECTED");
    if (path == nullptr || count == nullptr || result == nullptr) {
        return 1;
    }
    const auto iterations = std::strtoull(count, nullptr, 10);
    const auto expected = std::strtoull(result, nullptr, 10);
    auto stream = std::ifstream(path, std::ios::binary);
    if (!stream) {
        return 1;
    }
    const auto storage = std::vector<std::uint8_t>(
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    );
    const auto bytes = carven::runtime::Slice<std::uint8_t>(std::span(storage));
    if (carven::api::workload::validate_document(bytes) != expected) {
        return 2;
    }
    auto checksum = std::uint64_t {0};
    const auto begin = std::chrono::steady_clock::now();
    for (auto iteration = 0ull; iteration < iterations; ++iteration) {
        checksum += carven::api::workload::validate_document(bytes);
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - begin
    )
                             .count();
    if (checksum != expected * iterations) {
        return 3;
    }
    std::printf(
        "{\"elapsed_ns\":%lld,\"checksum\":%llu,\"expected\":%llu,\"iterations\":%llu,\"bytes\":%llu}\n",
        static_cast<long long>(elapsed),
        static_cast<unsigned long long>(checksum),
        static_cast<unsigned long long>(expected),
        static_cast<unsigned long long>(iterations),
        static_cast<unsigned long long>(storage.size())
    );
}
