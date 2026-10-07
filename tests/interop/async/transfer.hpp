#pragma once

#include "bridge.hpp"
#include <span>
#include <vector>

namespace async_transfer {

// The host owns descriptors and buffers through source completion/closure.
// Source Read borrows this holder; hidden native resources remain its contract.
class Context final {
public:
    async_interop::PipeQueue& queue;
    int fd;
    std::span<std::byte> buffer;
    async_bridge::Observations& observed;
    std::vector<std::size_t>& counts;
    const Context* host_address;

    Context(
        async_interop::PipeQueue& provider,
        int descriptor,
        std::span<std::byte> storage,
        async_bridge::Observations& state,
        std::vector<std::size_t>& transferred
    ) noexcept;
    Context(const Context&) = delete;
    Context(Context&&) = delete;
    auto operator=(const Context&) -> Context& = delete;
    auto operator=(Context&&) -> Context& = delete;
    ~Context() noexcept;
};

} // namespace async_transfer
