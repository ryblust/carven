#include <carven/runtime/text/text.hpp>

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
