#include <carven/std/channel/native.hpp>

#include <deque>
#include <optional>
#include <utility>

namespace carven::standard::channel {

class State final {
public:
    std::deque<runtime::Vector<std::uint8_t>> queue;
    std::size_t capacity;
    bool closed = false;

    explicit State(std::size_t capacity) noexcept
        : capacity(capacity) {}
};

auto Result::take_bytes() noexcept -> runtime::Vector<std::uint8_t> {
    return std::move(bytes);
}

auto create(std::size_t capacity) noexcept -> Handle {
    return {.state = std::make_shared<State>(capacity)};
}

auto close(const Handle& channel) noexcept -> void {
    channel.state->closed = true;
}

auto try_send(const Handle& channel, runtime::Vector<std::uint8_t> bytes) noexcept -> bool {
    auto& state = *channel.state;
    if (state.closed || state.queue.size() == state.capacity) {
        return false;
    }
    state.queue.push_back(std::move(bytes));
    return true;
}

auto try_receive(const Handle& channel) noexcept -> Result {
    auto& state = *channel.state;
    if (state.queue.empty()) {
        return {.bytes = {}, .available = false, .end = state.closed};
    }
    auto bytes = std::move(state.queue.front());
    state.queue.pop_front();
    return {.bytes = std::move(bytes), .available = true, .end = false};
}

namespace {

namespace async = runtime::execution;

class Request final : public async::EventSource {
    using Completion = async::Completion<Result>;
    Handle channel;
    bool sending;
    runtime::Vector<std::uint8_t> bytes;
    async::ResumeContinuation continuation;
    std::optional<Completion> result;
    bool attached = false;

    auto attempt() noexcept -> bool {
        if (continuation.cancel_requested()) {
            result.emplace(Completion::cancelled());
            return true;
        }
        auto& state = *channel.state;
        if (sending) {
            if (!state.closed && state.queue.size() == state.capacity) {
                return false;
            }
            if (!state.closed) {
                state.queue.push_back(std::move(bytes));
            }
            result.emplace(Completion::success_from([&]() noexcept {
                return Result {.bytes = {}, .available = !state.closed, .end = state.closed};
            }));
        } else {
            if (!state.closed && state.queue.empty()) {
                return false;
            }
            result.emplace(Completion::success_from([&]() noexcept {
                return try_receive(channel);
            }));
        }
        return true;
    }

public:
    Request(Handle channel, bool sending, runtime::Vector<std::uint8_t> bytes) noexcept
        : channel(std::move(channel)),
          sending(sending),
          bytes(std::move(bytes)) {}

    ~Request() noexcept override {
        if (attached) {
            continuation.driver().detach(*this);
        }
    }

    auto await_ready() const noexcept -> bool { return false; }

    template<typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> handle) noexcept -> bool {
        continuation.bind(handle);
        if (attempt()) {
            return false;
        }
        continuation.driver().attach(*this);
        attached = true;
        return true;
    }

    auto await_resume() noexcept -> Completion { return std::move(*result); }

    auto poll(async::Driver& driver) noexcept -> void override {
        if (!result && attempt()) {
            continuation.enqueue(driver);
        }
    }

    auto wait(std::optional<std::int64_t>) noexcept -> bool override { return false; }
};

} // namespace

auto send(Handle channel, runtime::Vector<std::uint8_t> bytes) noexcept
    -> async::AsyncOperation<Result> {
    co_return co_await Request(std::move(channel), true, std::move(bytes));
}

auto receive(Handle channel) noexcept -> async::AsyncOperation<Result> {
    co_return co_await Request(std::move(channel), false, {});
}

} // namespace carven::standard::channel
