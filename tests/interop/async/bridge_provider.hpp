#pragma once

#include "bridge.hpp"

#include <carven/providers/tests/interop/async/bridge.hpp>

namespace async_bridge {

namespace providers = carven::providers::tests::interop::async::bridge::cv_escaped_functions;

auto bridge_read_byte(const Context&) noexcept -> providers::bridge_read_byte::Completion;
auto bridge_write_byte(const Context&, std::int32_t) noexcept
    -> providers::bridge_write_byte::Completion;
auto bridge_trivial_byte(const TrivialContext&, std::int32_t) noexcept
    -> providers::bridge_trivial_byte::Completion;
auto bridge_note(const Context&, std::int32_t) noexcept -> void;
auto bridge_native_character(std::int32_t, bool) noexcept
    -> providers::bridge_native_character::Completion;

} // namespace async_bridge
