module carven:test.internal.source.text;

import :source.text;
import :test.harness.framework;
import std;

static_assert(SourceID::from_index(1) == SourceID::from_index(1));
static_assert(SourceID::from_index(1) != SourceID::from_index(2));

namespace {

const TestSuite suite([] static noexcept {
    "Source: slicing never escapes the borrowed snapshot"_test = [] static noexcept {
        static constexpr auto source = std::string_view("alpha beta");

        expect_equal(slice(source, Span::from_bounds(0, 5)), std::string_view("alpha"));
        expect_equal(slice(source, Span::from_bounds(6, 10)), std::string_view("beta"));
        expect_equal(slice(source, Span::at(5)), std::string_view(""));
        expect(!try_slice(source, Span::from_bounds(99, 100)).has_value());
        expect(!try_slice(source, Span::from_bounds(0, 100)).has_value());
    };
});

} // namespace
