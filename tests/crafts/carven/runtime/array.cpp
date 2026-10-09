#include <carven/runtime/array.hpp>

#include <array>

namespace {
constexpr auto site = carven::runtime::SourceSite::native();
} // namespace

constexpr auto array_header_contract() noexcept -> int {
    const auto values = std::array {1, 2};
    const auto adopted = carven::runtime::adopt_array<std::array<long, 2>, false>(values);
    return static_cast<int>(carven::runtime::checked_array_index(adopted, 1, site));
}

static_assert(array_header_contract() == 2);
