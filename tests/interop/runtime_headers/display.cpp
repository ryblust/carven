#include <carven/runtime/display.hpp>

static_assert(noexcept(carven::runtime::DisplayWriter()));

auto display_header_contract() noexcept -> bool {
    auto writer = carven::runtime::DisplayWriter();
    writer.quoted("text");
    return writer.result() == "\"text\"";
}
