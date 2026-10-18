#include "bridge_provider.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <span>

namespace {

namespace async = carven::runtime::execution;
namespace providers = async_bridge::providers;

using async_bridge::Context;
using async_bridge::Observations;
using async_bridge::TrivialContext;
using async_interop::PipeQueue;
using IOError = providers::bridge_read_byte::Failure0;
using ReadResult = async::Completion<std::int32_t, IOError>;

auto check(bool condition, const char* message) noexcept -> void {
    if (!condition) {
        std::fprintf(stderr, "async bridge contract failed: %s (errno=%d)\n", message, errno);
        std::exit(EXIT_FAILURE);
    }
}

class BufferLifetime final {
    Observations& observed;

public:
    explicit BufferLifetime(Observations& state) noexcept
        : observed(state) {
        ++observed.started;
        ++observed.live;
    }

    ~BufferLifetime() noexcept {
        --observed.live;
        ++observed.destroyed;
    }
};

auto read_completion(PipeQueue::Completion result, std::byte byte, Observations& observed) noexcept
    -> ReadResult {
    ++observed.finished;
    if (result.is_cancelled()) {
        ++observed.cancelled;
        return ReadResult::cancelled();
    }
    if (const auto* error = result.failure_if<async_interop::PipeError>()) {
        return ReadResult::failure(IOError {.code = error->code});
    }
    const auto* success = result.success_if();
    check(success != nullptr && success->value <= 1, "one-byte native read result");
    return ReadResult::success(success->value == 0 ? -1 : std::to_integer<std::int32_t>(byte));
}

} // namespace

async_bridge::Context::Context(PipeQueue& provider, int descriptor, Observations& state) noexcept
    : queue(provider),
      fd(descriptor),
      observed(state),
      host_address(this) {}

async_bridge::Context::~Context() noexcept {
    check(
        queue.pending_count() == 0 && observed.live == 0,
        "host context outlives provider terminal delivery and source closure"
    );
    ++observed.context_destroyed;
}

namespace async_bridge {

auto bridge_read_byte(const Context& context) noexcept -> providers::bridge_read_byte::Completion {
    check(&context == context.host_address, "noncopyable Read context is the host holder");
    const auto lifetime = BufferLifetime(context.observed);
    auto buffer = std::array<std::byte, 1> {};
    auto result = co_await context.queue.read_some(context.fd, buffer);
    co_return read_completion(std::move(result), buffer[0], context.observed);
}

auto bridge_write_byte(const Context& context, std::int32_t value) noexcept
    -> providers::bridge_write_byte::Completion {
    check(
        &context == context.host_address && value >= 0 && value <= 255,
        "valid write context and byte"
    );
    const auto lifetime = BufferLifetime(context.observed);
    const auto buffer = std::array {static_cast<std::byte>(value)};
    auto result = co_await context.queue.write_some(context.fd, buffer);
    ++context.observed.finished;
    using Result = async::Completion<void, IOError>;
    if (result.is_cancelled()) {
        ++context.observed.cancelled;
        co_return Result::cancelled();
    }
    if (const auto* error = result.failure_if<async_interop::PipeError>()) {
        co_return Result::failure(IOError {.code = error->code});
    }
    check(
        result.success_if() != nullptr && result.success_if()->value == 1,
        "one-byte native write result"
    );
    co_return Result::success();
}

auto bridge_trivial_byte(const TrivialContext& context, std::int32_t marker) noexcept
    -> providers::bridge_trivial_byte::Completion {
    check(
        &context == context.host_address && marker == 23,
        "trivial Read context retains host identity through factory and yielding argument"
    );
    const auto lifetime = BufferLifetime(*context.observed);
    auto buffer = std::array<std::byte, 1> {};
    auto result = co_await context.queue->read_some(context.fd, buffer);
    co_return read_completion(std::move(result), buffer[0], *context.observed);
}

auto bridge_note(const Context& context, std::int32_t stage) noexcept -> void {
    check(&context == context.host_address, "synchronous native Read borrows same host holder");
    if (stage == 2) {
        check(
            context.observed.started == 0,
            "stored imported operation does not start its native body"
        );
    }
    context.observed.trace = context.observed.trace * 10 + stage;
}

auto bridge_native_character(std::int32_t value, bool fail) noexcept
    -> providers::bridge_native_character::Completion {
    using Result = async::Completion<char32_t, IOError>;
    if (async::cancel_requested()) {
        co_return Result::cancelled();
    }
    if (fail) {
        co_return Result::failure(IOError {.code = EIO});
    }
    co_return Result::success(static_cast<char32_t>(value));
}

} // namespace async_bridge
