module carven:test.internal.support.utf8;

import :support.utf8;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Support UTF8 decoder: checks sequences and advances invalid bytes"_test = [] static noexcept {
        const auto text = std::string("a目录");
        const auto ascii = UTF8Decoder::decode(text, 0);
        const auto first = UTF8Decoder::decode(text, 1);

        expect(ascii.valid);
        expect_equal(ascii.width, 1u);
        expect(first.valid);
        expect_equal(first.width, 3u);
        expect_equal(static_cast<std::uint32_t>(first.scalar), static_cast<std::uint32_t>(U'目'));
        expect(UTF8Decoder::is_valid(text));

        const auto invalid = std::string("bad\xfftail");
        const auto sequence = UTF8Decoder::decode(invalid, 3);
        expect(!sequence.valid);
        expect_equal(sequence.width, 1u);
        expect(!UTF8Decoder::is_valid(invalid));
    };
});

} // namespace
