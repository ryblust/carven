#include <carven/runtime/passing.hpp>

auto passing_header_contract(carven::runtime::ReadArg<int> value) noexcept -> int {
    auto owned = value;
    return carven::runtime::transfer(owned);
}
