#include "provider.hpp"

auto staged_native_bias(std::int32_t value) noexcept -> std::int32_t {
    return value + 100;
}
