#pragma once

#include <carven/runtime/hosting/object.hpp>

#include <cstdint>

namespace native_values {

using Ref = carven::runtime::hosting::ObjectRef<std::int32_t>;

inline auto reference() noexcept -> Ref {
    return Ref::own(std::int32_t {7});
}

} // namespace native_values
