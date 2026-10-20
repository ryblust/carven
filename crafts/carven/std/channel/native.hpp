#pragma once

#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/execution/execution.hpp>

#include <cstdint>
#include <memory>

namespace carven::standard::channel {

class State;

struct Handle final {
    std::shared_ptr<State> state;
};

struct Result final {
    runtime::Vector<std::uint8_t> bytes;
    bool available;
    bool end;
    auto take_bytes() noexcept -> runtime::Vector<std::uint8_t>;
};

auto create(std::size_t capacity) noexcept -> Handle;
auto close(const Handle& channel) noexcept -> void;
auto try_send(const Handle& channel, runtime::Vector<std::uint8_t> bytes) noexcept -> bool;
auto try_receive(const Handle& channel) noexcept -> Result;

auto send(Handle channel, runtime::Vector<std::uint8_t> bytes) noexcept
    -> runtime::execution::AsyncOperation<Result>;

auto receive(Handle channel) noexcept -> runtime::execution::AsyncOperation<Result>;

} // namespace carven::standard::channel
