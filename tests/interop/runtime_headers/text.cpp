#include <carven/runtime/text.hpp>

#include <string_view>

auto text_header_contract(std::string_view text) noexcept -> char32_t {
    for (const auto scalar : carven::runtime::str_chars(carven::runtime::checked_utf8(text))) {
        return scalar;
    }
    return U'\0';
}

static_assert(carven::runtime::checked_utf8(std::string_view("a\0\xc3\xa9", 4)).size() == 4);
static_assert(carven::runtime::checked_unicode_scalar(U'\U0010ffff') == U'\U0010ffff');
static_assert([]() noexcept {
    auto sum = char32_t {0};
    for (const auto scalar : carven::runtime::str_chars("a\xc3\xa9\xe4\xbd\xa0\xf0\x9f\x98\x80")) {
        sum += scalar;
    }
    return sum == U'a' + U'\u00e9' + U'\u4f60' + U'\U0001f600';
}());
