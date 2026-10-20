#include "tail.hpp"

#include <carven/api/tests/interop/async/tail.hpp>

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {

std::int32_t trace = 0;
std::array<const void*, 8> addresses {};
std::size_t address_count = 0;
thread_local bool track_allocations = false;
thread_local std::size_t allocations = 0;

} // namespace

auto operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
    -> void* {
    if (track_allocations) {
        ++allocations;
    }
    return ::operator new(size, alignment);
}

auto operator delete(void* storage, std::align_val_t alignment, const std::nothrow_t&) noexcept
    -> void {
    ::operator delete(storage, alignment);
}

TailOwner::TailOwner(std::int32_t value) noexcept
    : digit(value) {}

TailOwner::~TailOwner() noexcept {
    tail_mark(digit);
}

auto tail_owner(std::int32_t digit) noexcept -> TailOwner {
    return TailOwner(digit);
}

auto tail_reset() noexcept -> void {
    trace = 0;
    address_count = 0;
}

auto tail_mark(std::int32_t digit) noexcept -> void {
    trace = trace * 10 + digit;
}

auto tail_actual(std::int32_t value, std::int32_t digit) noexcept -> std::int32_t {
    tail_mark(digit);
    return value;
}

auto tail_trace() noexcept -> std::int32_t {
    return trace;
}

auto tail_address(std::int32_t& value) noexcept -> void {
    for (std::size_t index = 0; index < address_count; ++index) {
        if (addresses[index] == &value) {
            return;
        }
    }
    addresses[address_count++] = &value;
}

auto tail_addresses() noexcept -> std::int32_t {
    return static_cast<std::int32_t>(address_count);
}

namespace {

struct FrameToken final {
    bool active = true;
    FrameToken() = default;
    FrameToken(const FrameToken&) = delete;

    FrameToken(FrameToken&& source) noexcept
        : active(source.active) {
        source.active = false;
    }

    ~FrameToken() noexcept {
        if (active) {
            tail_mark(4);
        }
    }
};

auto native_frame(FrameToken token) noexcept
    -> carven::runtime::execution::AsyncOperation<std::int32_t> {
    static_cast<void>(token);
    co_return carven::runtime::execution::Completion<std::int32_t>::success(1);
}

}

auto tail_native() noexcept -> carven::runtime::execution::AsyncOperation<std::int32_t> {
    // A movable frame parameter keeps observable cleanup until its operation
    // owner ends the enclosing full-expression.
    return native_frame(FrameToken {});
}

namespace {

using ScalarOperation = auto (*)(std::int32_t, std::int32_t) noexcept
    -> carven::runtime::execution::AsyncOperation<std::int32_t>;

auto operation_allocations(
    ScalarOperation operation,
    std::int32_t count,
    std::int32_t expected
) noexcept -> std::size_t {
    allocations = 0;
    track_allocations = true;
    const auto result = carven::runtime::execution::drive_root(operation(count, 0));
    track_allocations = false;
    const auto* success = result.success_if();
    if (success == nullptr || success->value != expected) {
        std::fputs("async operation returned an unexpected result\n", stderr);
        std::abort();
    }
    return allocations;
}

} // namespace

auto run_async_allocation_cases() noexcept -> void {
    const auto shallow =
        operation_allocations(carven::api::tests::interop::async::tail::bounded_tail, 4, 4);
    const auto deep =
        operation_allocations(carven::api::tests::interop::async::tail::bounded_tail, 8192, 8192);
    // Increasing recursion depth permits only fixed adapter allocation overhead.
    if (deep > shallow + 4) {
        std::fprintf(stderr, "tail recursion allocations grew from %zu to %zu\n", shallow, deep);
        std::abort();
    }
    const auto producer_shallow =
        operation_allocations(carven::api::tests::interop::async::tail::bounded_producer, 4, 4);
    const auto producer_deep = operation_allocations(
        carven::api::tests::interop::async::tail::bounded_producer,
        8192,
        8192
    );
    // Repeating an awaited producer must not add per-invocation frame allocations.
    if (producer_deep > producer_shallow + 4) {
        std::fprintf(
            stderr,
            "awaited producer allocations grew from %zu to %zu\n",
            producer_shallow,
            producer_deep
        );
        std::abort();
    }
}
