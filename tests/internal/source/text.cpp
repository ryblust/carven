module carven:test.internal.source.text;

import :source.text;
import :test.harness.framework;
import std;

static_assert(SourceID::from_index(1) == SourceID::from_index(1));
static_assert(SourceID::from_index(1) != SourceID::from_index(2));

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Source: slicing never escapes the borrowed snapshot", [] static noexcept {
        static constexpr auto source = std::string_view("alpha beta");

        ct::expect_equal(slice(source, Span::from_bounds(0, 5)), std::string_view("alpha"));
        ct::expect_equal(slice(source, Span::from_bounds(6, 10)), std::string_view("beta"));
        ct::expect_equal(slice(source, Span::at(5)), std::string_view(""));
        ct::expect(!try_slice(source, Span::from_bounds(99, 100)).has_value());
        ct::expect(!try_slice(source, Span::from_bounds(0, 100)).has_value());
    });
});

} // namespace
