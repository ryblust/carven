#include <carven/runtime/array.hpp>

#include <array>

constexpr auto array_header_contract() noexcept -> int {
    auto values = std::array {1, 2};
    const auto adopted = carven::runtime::adopt_array<std::array<long, 2>, false>(values);
    return static_cast<int>(carven::runtime::checked_array_index(adopted, 1));
}

static_assert(array_header_contract() == 2);
