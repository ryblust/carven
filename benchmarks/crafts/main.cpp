#include <carven/runtime/simd/simd.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <pthread/qos.h>
#endif

using Invoke = std::size_t (*)(unsigned, unsigned, const std::uint8_t*, std::size_t) noexcept;
extern "C" auto baseline_invoke(unsigned, unsigned, const std::uint8_t*, std::size_t) noexcept
    -> std::size_t;
extern "C" auto current_invoke(unsigned, unsigned, const std::uint8_t*, std::size_t) noexcept
    -> std::size_t;
extern "C" auto baseline_decode_equals(
    const std::uint8_t*,
    std::size_t,
    const char*,
    std::size_t
) noexcept -> bool;
extern "C" auto current_decode_equals(
    const std::uint8_t*,
    std::size_t,
    const char*,
    std::size_t
) noexcept -> bool;

namespace {

template<unsigned Set>
auto contains(std::uint8_t byte) noexcept -> bool {
    if constexpr (Set == 0) {
        return byte == 0;
    }
    if constexpr (Set == 1) {
        return byte == 32 || byte == 9 || byte == 10 || byte == 13;
    }
    return byte >= 48 && byte <= 57;
}

template<unsigned Set>
auto classify(carven::runtime::simd::U8x32 bytes) noexcept -> carven::runtime::simd::Mask32 {
    using Vector = carven::runtime::simd::U8x32;
    if constexpr (Set == 0) {
        return bytes == Vector::splat(0);
    }
    if constexpr (Set == 1) {
        return ((bytes - Vector::splat(9)) <= Vector::splat(1)) | (bytes == Vector::splat(13))
            | (bytes == Vector::splat(32));
    }
    return (bytes - Vector::splat(48)) <= Vector::splat(9);
}

template<unsigned Operation, unsigned Set, bool Vectorized>
auto scan(const std::uint8_t* data, std::size_t size) noexcept -> std::size_t {
    auto offset = std::size_t(0);
    auto count = std::size_t(0);
    if constexpr (Vectorized) {
        const auto bytes = carven::runtime::Slice<std::uint8_t>(std::span(data, size));
        while (size - offset >= 32) {
            auto mask = classify<Set>(carven::runtime::simd::U8x32::load(
                bytes,
                offset,
                carven::runtime::SourceSite::native()
            ));
            if constexpr (Operation == 0) {
                count += mask.count();
            } else {
                if constexpr (Operation == 2) {
                    mask = ~mask;
                }
                if (mask.any()) {
                    return offset + mask.first_or(32);
                }
            }
            offset += 32;
        }
    }
    while (offset < size) {
        const auto matched = contains<Set>(data[offset]);
        if constexpr (Operation == 0) {
            count += matched;
        } else if (matched == (Operation == 1)) {
            return offset;
        }
        ++offset;
    }
    return Operation == 0 ? count : size;
}

template<unsigned Operation, bool Vectorized>
auto dispatch_set(unsigned set, const std::uint8_t* data, std::size_t size) noexcept
    -> std::size_t {
    if (set == 0) {
        return scan<Operation, 0, Vectorized>(data, size);
    }
    if (set == 1) {
        return scan<Operation, 1, Vectorized>(data, size);
    }
    return scan<Operation, 2, Vectorized>(data, size);
}

template<bool Vectorized>
auto handwritten(
    unsigned operation,
    unsigned set,
    const std::uint8_t* data,
    std::size_t size
) noexcept -> std::size_t {
    if (operation == 0) {
        return dispatch_set<0, Vectorized>(set, data, size);
    }
    if (operation == 1) {
        return dispatch_set<1, Vectorized>(set, data, size);
    }
    return dispatch_set<2, Vectorized>(set, data, size);
}

template<unsigned Set, typename Sum, std::size_t BatchBytes>
auto bounded_count(const std::uint8_t* data, std::size_t size) noexcept -> std::size_t {
    auto offset = std::size_t(0);
    auto total = std::size_t(0);
    while (offset < size) {
        const auto end = offset + std::min(size - offset, BatchBytes);
        auto count = Sum(0);
        while (offset < end) {
            const auto byte = static_cast<unsigned>(data[offset]);
            auto matched = false;
            if constexpr (Set == 0) {
                matched = byte == 0;
            } else if constexpr (Set == 1) {
                matched = ((byte - 9u) <= 1u) | (byte == 13u) | (byte == 32u);
            } else {
                matched = (byte - 48u) <= 9u;
            }
            count += static_cast<Sum>(matched);
            ++offset;
        }
        total += count;
    }
    return total;
}

template<typename Sum, std::size_t BatchBytes>
auto bounded_loop(
    unsigned operation,
    unsigned set,
    const std::uint8_t* data,
    std::size_t size
) noexcept -> std::size_t {
    if (operation != 0) {
        return handwritten<false>(operation, set, data, size);
    }
    if (set == 0) {
        return bounded_count<0, Sum, BatchBytes>(data, size);
    }
    if (set == 1) {
        return bounded_count<1, Sum, BatchBytes>(data, size);
    }
    return bounded_count<2, Sum, BatchBytes>(data, size);
}

struct Case final {
    std::string name;
    unsigned operation;
    unsigned set;
    std::string input;
    std::size_t expected;
    std::string decoded;
    bool valid;
};

struct Implementation final {
    std::string_view name;
    Invoke invoke;
};

const auto implementations = std::vector<Implementation> {
    {.name = "baseline", .invoke = baseline_invoke},
    {.name = "current", .invoke = current_invoke},
    {.name = "handwritten_loop", .invoke = handwritten<false>},
    {.name = "handwritten_vector", .invoke = handwritten<true>},
    {.name = "bounded_u8", .invoke = bounded_loop<std::uint8_t, 255>},
    {.name = "bounded_u16", .invoke = bounded_loop<std::uint16_t, 32768>},
    {.name = "bounded_u32", .invoke = bounded_loop<std::uint32_t, 65536>},
};

auto repeat(std::string_view unit, std::size_t count) -> std::string {
    auto result = std::string();
    result.reserve(unit.size() * count);
    for (auto index = std::size_t(0); index < count; ++index) {
        result += unit;
    }
    return result;
}

auto cases() -> std::vector<Case> {
    auto result = std::vector<Case>();
    const auto names = std::vector<std::string> {"zero", "space", "digits"};
    const auto matching = std::vector<char> {'\0', ' ', '5'};
    for (auto set = 0u; set < 3; ++set) {
        for (const auto size :
             {0u,   1u,   7u,   31u,  32u,   33u,   63u,    64u,    65u,    127u,   128u,
              129u, 255u, 256u, 257u, 1024u, 8192u, 16319u, 16320u, 16321u, 65536u, 65537u}) {
            const auto accumulation_boundary = (size >= 16319 && size <= 16321) || size == 65537;
            if ((size == 8192 || accumulation_boundary) && set != 0) {
                continue;
            }
            for (const auto dense : {false, true}) {
                if (accumulation_boundary && !dense) {
                    continue;
                }
                auto input = std::string(size, 'x');
                auto count = std::size_t(0);
                for (auto index = std::size_t(0); index < size; ++index) {
                    if (dense || index % 97 == 13) {
                        input[index] = matching[set];
                        ++count;
                    }
                }
                result.push_back(
                    {.name = "count/" + names[set] + (dense ? "/dense" : "/sparse"),
                     .operation = 0,
                     .set = set,
                     .input = std::move(input),
                     .expected = count,
                     .decoded = {},
                     .valid = true}
                );
            }
        }
        for (const auto size : {1u, 31u, 33u, 1024u, 65536u}) {
            for (auto shape = 0u; shape < 3; ++shape) {
                const auto position = shape == 0 ? 0u : shape == 1 ? size - 1 : size;
                auto find = std::string(size, 'x');
                if (position < size) {
                    find[position] = matching[set];
                }
                result.push_back(
                    {.name = "find/" + names[set] + "/" + std::to_string(shape),
                     .operation = 1,
                     .set = set,
                     .input = std::move(find),
                     .expected = position,
                     .decoded = {},
                     .valid = true}
                );
                auto prefix = std::string(size, matching[set]);
                if (position < size) {
                    prefix[position] = 'x';
                }
                result.push_back(
                    {.name = "prefix/" + names[set] + "/" + std::to_string(shape),
                     .operation = 2,
                     .set = set,
                     .input = std::move(prefix),
                     .expected = position,
                     .decoded = {},
                     .valid = true}
                );
            }
        }
    }
    for (const auto size : {7u, 31u, 33u, 1024u, 65536u}) {
        for (const auto unicode : {false, true}) {
            const auto unit = unicode ? std::string_view("你😀é") : std::string_view("abcdefg");
            const auto input = repeat(unit, std::max<std::size_t>(1, size / unit.size()));
            result.push_back(
                {.name = unicode ? "utf/unicode" : "utf/ascii",
                 .operation = 3,
                 .set = 0,
                 .input = input,
                 .expected = 0,
                 .decoded = {},
                 .valid = true}
            );
        }
    }
    auto invalid_utf = std::string(4096, 'a');
    invalid_utf[3072] = static_cast<char>(0xff);
    result.push_back(
        {.name = "utf/invalid",
         .operation = 3,
         .set = 0,
         .input = invalid_utf,
         .expected = 3073,
         .decoded = {},
         .valid = false}
    );

    for (const auto size : {8u, 32u, 1024u, 65536u}) {
        const auto documents = std::vector<std::pair<std::string, std::string>> {
            {"plain", "\"" + std::string(size, 'a') + "\""},
            {"unicode", "\"" + repeat("你😀", std::max(1u, size / 7)) + "\""},
            {"escapes", "\"" + repeat("a\\n\\u0041", std::max(1u, size / 9)) + "\""},
            {"numbers", "[" + repeat("1,-12.5e+2,", std::max(1u, size / 11)) + "0]"},
            {"space", std::string(size, ' ') + "null"},
        };
        for (const auto& [name, input] : documents) {
            for (const auto operation : {4u, 5u}) {
                result.push_back(
                    {.name = "json/" + name + (operation == 4 ? "/text" : "/bytes"),
                     .operation = operation,
                     .set = 0,
                     .input = input,
                     .expected = 0,
                     .decoded = {},
                     .valid = true}
                );
            }
        }
        const auto decoded =
            std::vector<std::pair<std::string, std::pair<std::string, std::string>>> {
                {"plain", {"\"" + std::string(size, 'a') + "\"", std::string(size, 'a')}},
                {"escapes",
                 {"\"" + repeat("a\\n\\u0041", std::max(1u, size / 9)) + "\"",
                  repeat("a\nA", std::max(1u, size / 9))}},
        };
        for (const auto& [name, values] : decoded) {
            const auto expected =
                values.second.size() + static_cast<unsigned char>(values.second.back()) * 65536u;
            result.push_back(
                {.name = "decode/" + name,
                 .operation = 6,
                 .set = 0,
                 .input = values.first,
                 .expected = expected,
                 .decoded = values.second,
                 .valid = true}
            );
        }
    }
    const auto nested = repeat("[", 32) + "true" + repeat("]", 32);
    for (const auto operation : {4u, 5u}) {
        result.push_back(
            {.name = "json/nested",
             .operation = operation,
             .set = 0,
             .input = nested,
             .expected = 0,
             .decoded = {},
             .valid = true}
        );
        result.push_back(
            {.name = "json/invalid",
             .operation = operation,
             .set = 0,
             .input = "[1,]",
             .expected = 4,
             .decoded = {},
             .valid = false}
        );
    }
    result.push_back(
        {.name = "json/invalid_utf/bytes",
         .operation = 5,
         .set = 0,
         .input = invalid_utf,
         .expected = 3073,
         .decoded = {},
         .valid = false}
    );
    return result;
}

using Clock = std::chrono::steady_clock;

struct Batch final {
    double ns;
    std::size_t checksum;
};

auto measure(const Case& input, Invoke invoke, std::size_t iterations) noexcept -> Batch {
    const auto bytes = reinterpret_cast<const std::uint8_t*>(input.input.data());
    auto checksum = std::size_t(0);
    const auto start = Clock::now();
    for (auto index = std::size_t(0); index < iterations; ++index) {
        // Keep each call observable even if a handwritten implementation is inlined.
        // This compiler barrier emits no instruction and applies to every implementation.
        asm volatile("" : : "r"(bytes) : "memory");
        checksum += invoke(input.operation, input.set, bytes, input.input.size()) + index;
    }
    const auto elapsed = std::chrono::duration<double, std::nano>(Clock::now() - start).count();
    return {.ns = elapsed, .checksum = checksum};
}

} // namespace

auto main(int argc, char** argv) -> int {
    if (argc != 4 && argc != 5) {
        std::cerr << "usage: benchmark OUTPUT SAMPLES BATCH_MS [CASE_PREFIX]\n";
        return 2;
    }
#if defined(__APPLE__)
    const auto qos_status = pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
    if (qos_status != 0) {
        std::cerr << "setting sampling thread QoS failed: " << qos_status << '\n';
        return 1;
    }
    constexpr auto qos = "user_initiated";
#else
    constexpr auto qos = "default";
#endif
    const auto output = std::string(argv[1]);
    const auto samples = static_cast<unsigned>(std::stoul(argv[2]));
    const auto target_ns = std::stod(argv[3]) * 1e6;
    auto inputs = cases();
    const auto prefix = argc == 5 ? std::string_view(argv[4]) : std::string_view();
    std::erase_if(inputs, [prefix](const Case& value) { return !value.name.starts_with(prefix); });
    if (inputs.empty()) {
        std::cerr << "no cases match the requested prefix\n";
        return 2;
    }
    auto results = std::ofstream(output + "/results.csv");
    auto observations = std::ofstream(output + "/samples.csv");
    results
        << "case,operation,set,input_bytes,valid,implementation,iterations,median_ns,min_ns,max_ns,gib_s,checksum\n";
    observations << "case,input_bytes,implementation,sample,iterations,ns_per_op,checksum\n";
    results << std::setprecision(12);
    observations << std::setprecision(12);
    for (const auto& input : inputs) {
        const auto count = input.operation == 0 ? implementations.size()
            : input.operation < 3               ? std::size_t(4)
                                                : std::size_t(2);
        const auto bytes = reinterpret_cast<const std::uint8_t*>(input.input.data());
        for (auto implementation = std::size_t(0); implementation < count; ++implementation) {
            const auto actual = implementations[implementation]
                                    .invoke(input.operation, input.set, bytes, input.input.size());
            if (actual != input.expected) {
                std::cerr << input.name << " bytes=" << input.input.size() << ' '
                          << implementations[implementation].name << " expected=" << input.expected
                          << " actual=" << actual << '\n';
                return 1;
            }
        }
        if (input.operation == 6
            && (!baseline_decode_equals(
                    bytes,
                    input.input.size(),
                    input.decoded.data(),
                    input.decoded.size()
                )
                || !current_decode_equals(
                    bytes,
                    input.input.size(),
                    input.decoded.data(),
                    input.decoded.size()
                ))) {
            std::cerr << "decoded bytes differ: " << input.name << '\n';
            return 1;
        }
        auto iterations = std::vector<std::size_t>(count, 1);
        auto times = std::vector<std::vector<double>>(count);
        auto checksums = std::vector<std::size_t>(count, 0);
        for (auto implementation = std::size_t(0); implementation < count; ++implementation) {
            for (auto warmup = 0; warmup < 2; ++warmup) {
                checksums[implementation] +=
                    measure(input, implementations[implementation].invoke, 100).checksum;
            }
            while (true) {
                const auto batch = measure(
                    input,
                    implementations[implementation].invoke,
                    iterations[implementation]
                );
                checksums[implementation] += batch.checksum;
                if (batch.ns >= target_ns) {
                    break;
                }
                const auto growth = std::clamp(target_ns / std::max(batch.ns, 1.0), 2.0, 16.0);
                iterations[implementation] =
                    static_cast<std::size_t>(iterations[implementation] * growth) + 1;
            }
        }
        for (auto sample = 0u; sample < samples; ++sample) {
            for (auto ordinal = std::size_t(0); ordinal < count; ++ordinal) {
                const auto implementation = (ordinal + sample) % count;
                const auto batch = measure(
                    input,
                    implementations[implementation].invoke,
                    iterations[implementation]
                );
                const auto ns = batch.ns / iterations[implementation];
                times[implementation].push_back(ns);
                checksums[implementation] += batch.checksum;
                observations << input.name << ',' << input.input.size() << ','
                             << implementations[implementation].name << ',' << sample << ','
                             << iterations[implementation] << ',' << ns << ',' << batch.checksum
                             << '\n';
            }
        }
        for (auto implementation = std::size_t(0); implementation < count; ++implementation) {
            auto& values = times[implementation];
            std::sort(values.begin(), values.end());
            const auto median = values[values.size() / 2];
            const auto complete = input.valid
                && (input.operation == 0
                    || input.operation >= 3
                    || input.expected == input.input.size());
            const auto throughput = complete ? input.input.size() * 1e9 / median / 1073741824.0 : 0;
            results << input.name << ',' << input.operation << ',' << input.set << ','
                    << input.input.size() << ',' << input.valid << ','
                    << implementations[implementation].name << ',' << iterations[implementation]
                    << ',' << median << ',' << values.front() << ',' << values.back() << ','
                    << throughput << ',' << checksums[implementation] << '\n';
        }
    }
    std::cout << "backend="
              << (carven::runtime::simd::hardware_accelerated ? "accelerated" : "portable")
              << " qos=" << qos << " cases=" << inputs.size() << " samples=" << samples << '\n';
    return results && observations ? 0 : 1;
}
