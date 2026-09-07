#include <carven/runtime/numeric.hpp>

auto numeric_header_contract(int left, int right) noexcept -> int {
    return carven::runtime::integer_add(left, right);
}
