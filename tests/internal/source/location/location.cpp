module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.source.location;

import :source.location;
import :source.text;
import std;

TEST_CASE("Source: spans are half-open byte ranges") {
    static_assert(std::is_trivially_copyable_v<Span>);
    static_assert(!std::default_initializable<Span>);

    const auto span = Span::from_bounds(4, 9);
    CHECK_EQ(span.start(), 4u);
    CHECK_EQ(span.end(), 9u);
    CHECK_EQ(span.size(), 5u);
    CHECK(!span.empty());
    CHECK(Span::at(3).empty());
}

TEST_CASE("Source: line lookup uses one-based lines and byte columns") {
    const auto index = LineIndex("first\r\nsecond\n三");

    CHECK_EQ(index.location(0).line, 1u);
    CHECK_EQ(index.location(0).column, 1u);
    CHECK_EQ(index.location(7).line, 2u);
    CHECK_EQ(index.location(7).column, 1u);
    CHECK_EQ(index.location(13).line, 2u);
    CHECK_EQ(index.location(13).column, 7u);
    CHECK_EQ(index.location(14).line, 3u);
    CHECK_EQ(index.location(14).column, 1u);
    CHECK_EQ(index.location(17).line, 3u);
    CHECK_EQ(index.location(17).column, 4u);
    CHECK_EQ(index.location(99).line, 3u);
    CHECK_EQ(index.location(99).column, 4u);
}
