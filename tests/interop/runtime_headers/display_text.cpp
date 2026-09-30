#include <carven/runtime/display/text.hpp>

static_assert(noexcept(carven::runtime::DisplayText()));

auto display_text_header_contract() noexcept -> bool {
    auto text = carven::runtime::DisplayText();
    text.quoted("text");
    return text.result() == "\"text\"";
}
