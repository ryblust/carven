#include <carven/runtime/text.hpp>

#include <string_view>

static_assert(carven::runtime::utf8_is_valid(std::string_view {"Carven"}));
static_assert(carven::runtime::checked_utf8("Carven", "invalid test text") == "Carven");

auto text_header_contract(std::string_view text) noexcept -> char32_t {
    for (const auto scalar : carven::runtime::str_chars(text)) {
        return scalar;
    }
    return U'\0';
}
