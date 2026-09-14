module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/text.hpp>

module carven:test.internal.runtime.text;

import std;

TEST_CASE("Runtime: text views expose bytes and Unicode scalar values") {
    const auto text = std::string_view("a\xc3\xa9\xe4\xbd\xa0\xf0\x9f\x98\x80", 10);
    auto bytes = std::vector<std::uint8_t>();
    for (const auto byte : carven::runtime::text_bytes(text)) {
        bytes.push_back(byte);
    }
    REQUIRE_EQ(bytes.size(), 10u);
    CHECK_EQ(bytes.front(), 0x61u);
    CHECK_EQ(bytes.back(), 0x80u);

    auto characters = std::vector<char32_t>();
    for (const auto character : carven::runtime::text_chars(text)) {
        characters.push_back(character);
    }
    REQUIRE_EQ(characters.size(), 4u);
    CHECK_EQ(characters[0], U'a');
    CHECK_EQ(characters[1], U'\u00e9');
    CHECK_EQ(characters[2], U'\u4f60');
    CHECK_EQ(characters[3], U'\U0001f600');
}

TEST_CASE("Runtime: empty text has no scalar to dereference") {
    const auto empty = carven::runtime::text_chars("");
    CHECK_FALSE(empty.begin() != empty.end());
}

TEST_CASE("Runtime: checked UTF-8 preserves the borrowed byte range") {
    const auto text = std::string_view("a\0\xc3\xa9", 4);
    const auto checked = carven::runtime::checked_utf8(text);
    CHECK(checked.data() == text.data());
    CHECK(checked.size() == text.size());
    CHECK(carven::runtime::checked_utf8({}).empty());
}

TEST_CASE("Runtime: UTF representation conversion preserves borrowed byte storage") {
    const auto bytes = std::array<std::uint8_t, 3> {65, 0, 66};
    const auto input = carven::runtime::Slice<std::uint8_t>(std::span(bytes));
    const auto text = carven::runtime::utf8_text(input);
    CHECK_EQ(text, std::string_view("A\0B", 3));
    CHECK(text.data() == reinterpret_cast<const char*>(bytes.data()));
}

TEST_CASE("Runtime: String byte views borrow the owner's storage") {
    const auto text = carven::runtime::String::from_str("owning text with retained backing");
    const auto bytes = carven::runtime::text_bytes(text);
    CHECK(bytes.data() == reinterpret_cast<const std::uint8_t*>(text.as_str().data()));
    CHECK(bytes.size() == text.size());
}
