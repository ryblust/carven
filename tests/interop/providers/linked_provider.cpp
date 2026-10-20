#include "providers/providers.hpp"

#include <carven/providers/tests/interop/providers/header_bridge.hpp>

auto linked_offset(std::int32_t value) noexcept -> std::int32_t {
    return value + 2;
}

namespace carven::providers::tests::interop::providers::header_bridge::cv_escaped_functions::
    twice_via_header {

auto invoke(Argument0 value) noexcept -> Completion {
    return Completion::success(value * 2);
}

} // namespace carven::providers::tests::interop::providers::header_bridge::cv_escaped_functions::twice_via_header
