#pragma once

#include "pipe.hpp"
#include <cstdint>
#include <type_traits>

namespace async_bridge {

struct Observations final {
    int started;
    int finished;
    int cancelled;
    int live;
    int destroyed;
    int context_destroyed;
    int trace;
};

// The host owns the queue and descriptor. Read borrows this holder; it does not
// establish the lifetimes of the native resources reached through it.
class Context final {
public:
    async_interop::PipeQueue& queue;
    int fd;
    Observations& observed;
    const Context* host_address;

    Context(async_interop::PipeQueue& provider, int descriptor, Observations& state) noexcept;
    Context(const Context&) = delete;
    Context(Context&&) = delete;
    auto operator=(const Context&) -> Context& = delete;
    auto operator=(Context&&) -> Context& = delete;
    ~Context() noexcept;
};

struct TrivialContext final {
    async_interop::PipeQueue* queue;
    int fd;
    Observations* observed;
    const TrivialContext* host_address;
};

static_assert(std::is_trivially_copyable_v<TrivialContext>);
static_assert(std::is_trivially_destructible_v<TrivialContext>);

} // namespace async_bridge

auto bridge_scalar_character_calls() noexcept -> int;
