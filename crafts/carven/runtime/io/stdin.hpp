#pragma once

#include <carven/runtime/io/queue.hpp>

#include <cstddef>
#include <cstdint>

namespace carven::runtime::io {

// The tuple carries bytes, EOF, a system error, and the limit-exceeded flag.
// A complete request owns its bytes before the cursor commits consumption.
inline auto read_input(std::uint8_t mode, std::size_t limit) noexcept
    -> execution::AsyncOperation<detail::ReadResult> {
    co_return co_await detail::Queue::process().read(static_cast<detail::ReadMode>(mode), limit);
}

} // namespace carven::runtime::io
