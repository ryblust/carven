#include <carven/runtime/slice.hpp>

#include <cstdint>

static_assert([]() noexcept {
    const auto values = std::array {10, 20, 30};
    const auto view = carven::runtime::as_slice(values);
    return view.size() == 3
        && view[1] == 20
        && view.slice(1, 3)[0] == 20
        && view.slice(3, 3).empty();
}());

static_assert([]() noexcept {
    const auto bytes = std::array<std::uint8_t, 3> {0, 128, 255};
    const auto view = carven::runtime::as_slice(bytes);
    auto sum = 0;
    for (const auto byte : view) {
        sum += byte;
    }
    return view[0] == 0 && view[2] == 255 && sum == 383 && view.slice(3, 3).empty();
}());
