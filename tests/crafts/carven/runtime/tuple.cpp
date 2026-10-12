#include <carven/runtime/value/tuple.hpp>

auto tuple_header_contract() noexcept -> int {
    auto value = carven::runtime::Tuple<int, carven::runtime::Void> {42, {{}, {}}};
    return carven::runtime::tuple_get<0>(value);
}
