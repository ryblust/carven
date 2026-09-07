#include <carven/runtime/array.hpp>

#include <array>

constexpr auto array_header_contract() noexcept -> int {
    auto values = std::array {1, 2};
    return carven::runtime::checked_array_index(values, 1);
}
