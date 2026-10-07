#include "probe.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <limits>
#include <new>

namespace {

struct Configuration final {
    std::uint64_t iterations;
    std::uint64_t seed;
    std::uint64_t depth;
    bool observe_stack;
};

auto environment_integer(const char* name, std::uint64_t fallback) noexcept -> std::uint64_t {
    const auto* text = std::getenv(name);
    if (text == nullptr) {
        return fallback;
    }
    auto* end = static_cast<char*>(nullptr);
    const auto value = std::strtoull(text, &end, 10);
    if (*text == '\0' || *end != '\0') {
        std::abort();
    }
    return value;
}

const auto configuration = Configuration {
    .iterations = environment_integer("CARVEN_ASYNC_BENCH_ITERATIONS", 100000),
    .seed = environment_integer("CARVEN_ASYNC_BENCH_SEED", 42),
    .depth = environment_integer("CARVEN_ASYNC_BENCH_DEPTH", 1024),
    .observe_stack = environment_integer("CARVEN_ASYNC_BENCH_STACK", 0) != 0,
};

struct Measurements final {
    bool active;
    bool finished;
    std::uint64_t checksum;
    std::uint64_t allocations;
    std::uint64_t allocated_bytes;
    std::uint64_t live_bytes;
    std::uint64_t peak_live_bytes;
    std::uintptr_t lowest_stack;
    std::uintptr_t highest_stack;
    std::chrono::steady_clock::time_point started;
    std::uint64_t elapsed_ns;
};

Measurements measurements {
    .active = false,
    .finished = false,
    .checksum = 0,
    .allocations = 0,
    .allocated_bytes = 0,
    .live_bytes = 0,
    .peak_live_bytes = 0,
    .lowest_stack = std::numeric_limits<std::uintptr_t>::max(),
    .highest_stack = 0,
    .started = {},
    .elapsed_ns = 0,
};

struct Reporter final {
    ~Reporter() noexcept;
};

Reporter::~Reporter() noexcept {
    if (!measurements.finished) {
        return;
    }
    const auto stack_span = measurements.highest_stack == 0
        ? 0
        : measurements.highest_stack - measurements.lowest_stack;
    std::printf(
        "{\"checksum\":%llu,\"elapsed_ns\":%llu,\"allocations\":%llu,"
        "\"allocated_bytes\":%llu,\"peak_live_bytes\":%llu,"
        "\"remaining_live_bytes\":%llu,\"stack_span_bytes\":%llu}\n",
        static_cast<unsigned long long>(measurements.checksum),
        static_cast<unsigned long long>(measurements.elapsed_ns),
        static_cast<unsigned long long>(measurements.allocations),
        static_cast<unsigned long long>(measurements.allocated_bytes),
        static_cast<unsigned long long>(measurements.peak_live_bytes),
        static_cast<unsigned long long>(measurements.live_bytes),
        static_cast<unsigned long long>(stack_span)
    );
}

Reporter reporter;

#if CARVEN_ASYNC_BENCH_ALLOCATIONS
struct AllocationHeader final {
    void* base;
    std::size_t bytes;
    bool counted;
};

auto allocate(std::size_t bytes, std::size_t alignment) noexcept -> void* {
    alignment = std::max(alignment, alignof(AllocationHeader));
    const auto actual = std::max(bytes, std::size_t {1});
    if (actual > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader) - alignment) {
        return nullptr;
    }
    auto* base = std::malloc(actual + sizeof(AllocationHeader) + alignment);
    if (base == nullptr) {
        return nullptr;
    }
    const auto begin = reinterpret_cast<std::uintptr_t>(base) + sizeof(AllocationHeader);
    const auto address = (begin + alignment - 1) & ~(alignment - 1);
    auto* header = reinterpret_cast<AllocationHeader*>(address - sizeof(AllocationHeader));
    *header = AllocationHeader {.base = base, .bytes = bytes, .counted = measurements.active};
    if (header->counted) {
        ++measurements.allocations;
        measurements.allocated_bytes += bytes;
        measurements.live_bytes += bytes;
        measurements.peak_live_bytes =
            std::max(measurements.peak_live_bytes, measurements.live_bytes);
    }
    return reinterpret_cast<void*>(address);
}

auto release(void* pointer) noexcept -> void {
    if (pointer == nullptr) {
        return;
    }
    const auto* header = reinterpret_cast<const AllocationHeader*>(
        reinterpret_cast<std::uintptr_t>(pointer) - sizeof(AllocationHeader)
    );
    if (header->counted) {
        measurements.live_bytes -= header->bytes;
    }
    std::free(header->base);
}

// Replacement operator new retains its standard exception specification.
auto checked_allocate(std::size_t bytes, std::size_t alignment) noexcept -> void* {
    auto* result = allocate(bytes, alignment);
    if (result == nullptr) {
        std::terminate();
    }
    return result;
}
#endif

} // namespace

#if CARVEN_ASYNC_BENCH_ALLOCATIONS
auto operator new(std::size_t bytes) -> void* {
    return checked_allocate(bytes, alignof(std::max_align_t));
}

auto operator new[](std::size_t bytes) -> void* {
    return checked_allocate(bytes, alignof(std::max_align_t));
}

auto operator new(std::size_t bytes, std::align_val_t alignment) -> void* {
    return checked_allocate(bytes, static_cast<std::size_t>(alignment));
}

auto operator new[](std::size_t bytes, std::align_val_t alignment) -> void* {
    return checked_allocate(bytes, static_cast<std::size_t>(alignment));
}

auto operator new(std::size_t bytes, const std::nothrow_t&) noexcept -> void* {
    return allocate(bytes, alignof(std::max_align_t));
}

auto operator new[](std::size_t bytes, const std::nothrow_t&) noexcept -> void* {
    return allocate(bytes, alignof(std::max_align_t));
}

auto operator new(std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
    -> void* {
    return allocate(bytes, static_cast<std::size_t>(alignment));
}

auto operator new[](std::size_t bytes, std::align_val_t alignment, const std::nothrow_t&) noexcept
    -> void* {
    return allocate(bytes, static_cast<std::size_t>(alignment));
}

auto operator delete(void* pointer) noexcept -> void {
    release(pointer);
}

auto operator delete[](void* pointer) noexcept -> void {
    release(pointer);
}

auto operator delete(void* pointer, std::size_t) noexcept -> void {
    release(pointer);
}

auto operator delete[](void* pointer, std::size_t) noexcept -> void {
    release(pointer);
}

auto operator delete(void* pointer, std::align_val_t) noexcept -> void {
    release(pointer);
}

auto operator delete[](void* pointer, std::align_val_t) noexcept -> void {
    release(pointer);
}

auto operator delete(void* pointer, std::size_t, std::align_val_t) noexcept -> void {
    release(pointer);
}

auto operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept -> void {
    release(pointer);
}

auto operator delete(void* pointer, const std::nothrow_t&) noexcept -> void {
    release(pointer);
}

auto operator delete[](void* pointer, const std::nothrow_t&) noexcept -> void {
    release(pointer);
}

auto operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept -> void {
    release(pointer);
}

auto operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept -> void {
    release(pointer);
}
#endif

namespace async_probe {

auto iterations() noexcept -> std::uint64_t {
    return configuration.iterations;
}

auto seed() noexcept -> std::uint64_t {
    return configuration.seed;
}

auto depth() noexcept -> std::uint64_t {
    return configuration.depth;
}

auto begin() noexcept -> void {
    measurements.active = true;
    measurements.started = std::chrono::steady_clock::now();
}

auto finish(std::uint64_t checksum) noexcept -> void {
    const auto ended = std::chrono::steady_clock::now();
    measurements.active = false;
    measurements.elapsed_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(ended - measurements.started).count()
    );
    measurements.checksum = checksum;
    measurements.finished = true;
}

#if defined(__clang__) || defined(__GNUC__)
__attribute__((noinline))
#endif
auto observe_stack() noexcept -> void {
    if (!configuration.observe_stack) {
        return;
    }
    volatile auto marker = 0ull;
    const auto address = reinterpret_cast<std::uintptr_t>(&marker);
    measurements.lowest_stack = std::min(measurements.lowest_stack, address);
    measurements.highest_stack = std::max(measurements.highest_stack, address);
}

} // namespace async_probe
