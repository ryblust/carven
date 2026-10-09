#include <carven/runtime/display/display.hpp>

static_assert(noexcept(carven::runtime::DisplayWriter()));

auto display_header_contract() noexcept -> bool {
    auto writer = carven::runtime::DisplayWriter();
    writer.quoted("text");
    carven::runtime::stateless_value<carven::runtime::ScalarDisplay>(writer, 1, std::size_t {0});
    return writer.result() == "\"text\"1";
}
