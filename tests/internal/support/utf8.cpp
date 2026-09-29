module carven:test.internal.support.utf8;

import :support.utf8;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Support UTF8 decoder: checks sequences and advances invalid bytes",
        [] static noexcept {
            const auto text = std::string("a目录");
            const auto ascii = UTF8Decoder::decode(text, 0);
            const auto first = UTF8Decoder::decode(text, 1);

            ct::expect(ascii.valid);
            ct::expect_equal(ascii.width, 1u);
            ct::expect(first.valid);
            ct::expect_equal(first.width, 3u);
            ct::expect_equal(
                static_cast<std::uint32_t>(first.scalar),
                static_cast<std::uint32_t>(U'目')
            );
            ct::expect(UTF8Decoder::is_valid(text));

            const auto invalid = std::string("bad\xfftail");
            const auto sequence = UTF8Decoder::decode(invalid, 3);
            ct::expect(!sequence.valid);
            ct::expect_equal(sequence.width, 1u);
            ct::expect(!UTF8Decoder::is_valid(invalid));
        }
    );
});

} // namespace
