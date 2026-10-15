#pragma once

#include "transfer.hpp"

#include <carven/providers/tests/interop/async/transfer.hpp>

namespace async_transfer {

namespace providers = carven::providers::tests::interop::async::transfer::cv_escaped_functions;

} // namespace async_transfer

auto transfer_read(const async_transfer::Context&) noexcept
    -> async_transfer::providers::transfer_read::Completion;
auto transfer_write(const async_transfer::Context&, std::int32_t, std::int32_t) noexcept
    -> async_transfer::providers::transfer_write::Completion;
auto transfer_copy_buffer(
    const async_transfer::Context&,
    const async_transfer::Context&,
    std::int32_t
) noexcept -> void;
