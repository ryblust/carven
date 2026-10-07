#include "transfer.hpp"
#include <carven/api/tests/interop/async/bridge.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace {
namespace async = carven::runtime::async;
using async_transfer::Context;
using IoError = decltype(carven::api::tests::interop::async::bridge::make_io_error(0));
using Result = async::Completion<std::int32_t, IoError>;

auto check(bool valid, const char* message) noexcept -> void {
    if (!valid) {
        std::fprintf(stderr, "async transfer contract failed: %s\n", message);
        std::exit(EXIT_FAILURE);
    }
}

class BufferLoan final {
    async_bridge::Observations& observed;

public:
    explicit BufferLoan(async_bridge::Observations& state) noexcept
        : observed(state) {
        ++observed.started;
        ++observed.live;
    }

    ~BufferLoan() noexcept {
        --observed.live;
        ++observed.destroyed;
    }
};

auto complete(async_interop::PipeQueue::Completion result, const Context& context) noexcept
    -> Result {
    ++context.observed.finished;
    if (result.is_cancelled()) {
        ++context.observed.cancelled;
        return Result::cancelled();
    }
    if (const auto* error = result.failure_if<async_interop::PipeError>()) {
        return Result::failure(
            carven::api::tests::interop::async::bridge::make_io_error(error->code)
        );
    }
    const auto* success = result.success_if();
    check(
        success != nullptr && success->value <= context.buffer.size(),
        "bounded actual byte count"
    );
    context.counts.push_back(success->value);
    return Result::success(static_cast<std::int32_t>(success->value));
}
} // namespace

async_transfer::Context::Context(
    async_interop::PipeQueue& provider,
    int descriptor,
    std::span<std::byte> storage,
    async_bridge::Observations& state,
    std::vector<std::size_t>& transferred
) noexcept
    : queue(provider),
      fd(descriptor),
      buffer(storage),
      observed(state),
      counts(transferred),
      host_address(this) {
    check(
        !buffer.empty()
            && buffer.size() <= static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max()),
        "nonempty representable host buffer"
    );
}

async_transfer::Context::~Context() noexcept {
    check(
        queue.pending_count() == 0 && observed.live == 0,
        "host resources outlive terminal delivery and closure"
    );
    ++observed.context_destroyed;
}

auto transfer_read(const Context& context) noexcept -> async::Operation<std::int32_t, IoError> {
    check(&context == context.host_address, "read borrows host holder");
    auto loan = BufferLoan(context.observed);
    auto result = co_await context.queue.read_some(context.fd, context.buffer);
    co_return complete(std::move(result), context);
}

auto transfer_write(const Context& context, std::int32_t offset, std::int32_t count) noexcept
    -> async::Operation<std::int32_t, IoError> {
    check(
        &context == context.host_address
            && offset >= 0
            && count > 0
            && static_cast<std::size_t>(offset) <= context.buffer.size()
            && static_cast<std::size_t>(count)
                <= context.buffer.size() - static_cast<std::size_t>(offset),
        "write cursor selects nonempty host buffer range"
    );
    auto loan = BufferLoan(context.observed);
    const auto slice =
        context.buffer.subspan(static_cast<std::size_t>(offset), static_cast<std::size_t>(count));
    auto result = co_await context.queue.write_some(context.fd, slice);
    if (const auto* success = result.success_if()) {
        check(
            success->value > 0 && success->value <= slice.size(),
            "write reports actual progress"
        );
    }
    co_return complete(std::move(result), context);
}

auto transfer_copy_buffer(const Context& writer, const Context& reader, std::int32_t count) noexcept
    -> void {
    check(
        count > 0
            && static_cast<std::size_t>(count) <= reader.buffer.size()
            && static_cast<std::size_t>(count) <= writer.buffer.size()
            && writer.observed.live == 0
            && reader.observed.live == 0,
        "completed read is copied before next buffer loan"
    );
    std::copy_n(reader.buffer.begin(), count, writer.buffer.begin());
}
