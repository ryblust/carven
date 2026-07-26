module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/runtime.hpp>

module carven:test.internal.runtime.text;

import std;

TEST_CASE("Runtime: text views expose bytes and Unicode scalar values") {
    const auto text = std::string_view("a\xc3\xa9\xe4\xbd\xa0\xf0\x9f\x98\x80", 10);
    auto bytes = std::vector<std::uint8_t>();
    for (const auto byte : carven::runtime::str_bytes(text)) {
        bytes.push_back(byte);
    }
    REQUIRE_EQ(bytes.size(), 10u);
    CHECK_EQ(bytes.front(), 0x61u);
    CHECK_EQ(bytes.back(), 0x80u);

    auto characters = std::vector<char32_t>();
    for (const auto character : carven::runtime::str_chars(text)) {
        characters.push_back(character);
    }
    REQUIRE_EQ(characters.size(), 4u);
    CHECK_EQ(characters[0], U'a');
    CHECK_EQ(characters[1], U'\u00e9');
    CHECK_EQ(characters[2], U'\u4f60');
    CHECK_EQ(characters[3], U'\U0001f600');
}

TEST_CASE("Runtime: UTF-8 decoder rejects malformed scalar encodings") {
    const auto rejects = [](std::string_view bytes) static noexcept {
        return !carven::runtime::detail::try_decode_utf8(bytes.data(), bytes.data() + bytes.size())
                    .has_value();
    };
    CHECK(rejects(std::string_view("\xc2", 1)));
    CHECK(rejects(std::string_view("\xc0\x80", 2)));
    CHECK(rejects(std::string_view("\xed\xa0\x80", 3)));
    CHECK(rejects(std::string_view("\xf4\x90\x80\x80", 4)));
    CHECK(rejects(std::string_view("\xe2\x28\xa1", 3)));
}
