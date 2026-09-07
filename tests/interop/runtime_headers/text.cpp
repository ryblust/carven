#include <carven/runtime/text.hpp>

#include <string_view>

auto text_header_contract(std::string_view text) noexcept -> char32_t {
    for (const auto scalar :
         carven::runtime::str_chars(carven::runtime::checked_utf8(text, "invalid text"))) {
        return scalar;
    }
    return U'\0';
}
