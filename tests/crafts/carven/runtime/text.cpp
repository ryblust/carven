#include <carven/runtime/text/text.hpp>

#include <string_view>

namespace {
constexpr auto site = carven::runtime::SourceSite::native();
} // namespace

auto text_header_contract(std::string_view text) noexcept -> char32_t {
    for (const auto scalar :
         carven::runtime::text_chars(carven::runtime::checked_utf8(text, site))) {
        return scalar;
    }
    return U'\0';
}

static_assert(carven::runtime::checked_utf8(std::string_view("a\0\xc3\xa9", 4), site).size() == 4);
static_assert(carven::runtime::checked_unicode_scalar(U'\U0010ffff', site) == U'\U0010ffff');
static_assert([]() noexcept {
    auto sum = char32_t {0};
    for (const auto scalar : carven::runtime::text_chars("a\xc3\xa9\xe4\xbd\xa0\xf0\x9f\x98\x80")) {
        sum += scalar;
    }
    return sum == U'a' + U'\u00e9' + U'\u4f60' + U'\U0001f600';
}());

auto owning_text_header_contract(const carven::runtime::String& text) noexcept -> std::size_t {
    const auto bytes = carven::runtime::text_bytes(text);
    const auto characters = carven::runtime::text_chars(text);
    return characters.begin() != characters.end() ? bytes.size() : 0;
}

auto string_header_contract() noexcept -> bool {
    auto text = carven::runtime::String::from_str("text");
    text.push(U'!');
    return text.as_str() == "text!";
}

static_assert([]() noexcept {
    auto text = carven::runtime::String::from_utf8("a");
    text.push(U'我');
    return text.as_str() == "a我";
}());

static_assert(carven::runtime::encode_valid_utf8(U'\U0001f600').width == 4);
static_assert(carven::runtime::decode_valid_utf8(std::span<const char>("A", 1)).scalar == U'A');
static_assert(!carven::runtime::utf8_is_valid("\xc0\x80"));
