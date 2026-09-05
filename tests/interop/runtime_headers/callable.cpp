#include <carven/runtime/callable.hpp>

namespace {

auto increment(int value) noexcept -> int {
    return value + 1;
}

} // namespace

auto callable_header_contract() noexcept -> int {
    const auto callable = carven::runtime::FunctionRef<int(int) noexcept>(increment);
    return callable(41);
}
