module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.support.utf8;

import :support.utf8;
import std;

TEST_CASE("Support UTF8 decoder: checks sequences and advances invalid bytes") {
    const auto text = std::string("a目录");
    const auto ascii = UTF8Decoder::decode(text, 0);
    const auto first = UTF8Decoder::decode(text, 1);

    CHECK(ascii.valid);
    CHECK_EQ(ascii.width, 1u);
    CHECK(first.valid);
    CHECK_EQ(first.width, 3u);
    CHECK_EQ(first.scalar, U'目');
    CHECK(UTF8Decoder::is_valid(text));

    const auto invalid = std::string("bad\xfftail");
    const auto sequence = UTF8Decoder::decode(invalid, 3);
    CHECK(!sequence.valid);
    CHECK_EQ(sequence.width, 1u);
    CHECK(!UTF8Decoder::is_valid(invalid));
}
