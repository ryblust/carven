module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.source.text;

import :source.text;
import std;

static_assert(SourceID::from_index(1) == SourceID::from_index(1));
static_assert(SourceID::from_index(1) != SourceID::from_index(2));

TEST_CASE("Source: slicing never escapes the borrowed snapshot") {
    static constexpr auto source = std::string_view("alpha beta");

    CHECK_EQ(slice(source, Span::from_bounds(0, 5)), "alpha");
    CHECK_EQ(slice(source, Span::from_bounds(6, 10)), "beta");
    CHECK_EQ(slice(source, Span::at(5)), "");
    CHECK(!try_slice(source, Span::from_bounds(99, 100)).has_value());
    CHECK(!try_slice(source, Span::from_bounds(0, 100)).has_value());
}

TEST_CASE("Source view: memory input keeps text and origin on one borrowed path") {
    const auto storage = std::string("fn main() {}");
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = storage,
        .origin = "memory-test.cv",
    };

    CHECK_EQ(source.text, "fn main() {}");
    CHECK_EQ(source.origin, "memory-test.cv");
}
