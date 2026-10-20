#pragma once

#include <carven/runtime/collection/vector.hpp>
#include <carven/runtime/execution/execution.hpp>

#include <cstdint>
#include <string_view>

namespace carven::standard::io {

auto write_stdout(std::string_view bytes) noexcept -> std::int32_t;

auto write_output(runtime::Vector<std::uint8_t> bytes) noexcept
    -> runtime::execution::AsyncOperation<std::int32_t>;

} // namespace carven::standard::io
