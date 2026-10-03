#include <carven/runtime/stateless.hpp>

namespace {

struct TypeToken final {};

struct Double final {
    constexpr auto operator()(int value) const noexcept -> int { return value * 2; }
};

static_assert(carven::runtime::Stateless<TypeToken>);
static_assert(carven::runtime::Stateless<Double>);
static_assert(!carven::runtime::Stateless<const Double>);
static_assert(carven::runtime::stateless_value<Double>(3) == 6);

} // namespace

auto stateless_header_contract() noexcept -> int {
    return carven::runtime::stateless_value<Double>(3);
}
