module;
#include <carven/runtime/text.hpp>

module carven:test.internal.runtime.text;

import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Runtime: text views expose bytes and Unicode scalar values", [] static noexcept {
        const auto text = std::string_view("a\xc3\xa9\xe4\xbd\xa0\xf0\x9f\x98\x80", 10);
        auto bytes = std::vector<std::uint8_t>();
        for (const auto byte : carven::runtime::text_bytes(text)) {
            bytes.push_back(byte);
        }
        if (!ct::expect_equal(bytes.size(), 10u)) {
            return;
        }
        ct::expect_equal(bytes.front(), 0x61u);
        ct::expect_equal(bytes.back(), 0x80u);

        auto characters = std::vector<char32_t>();
        for (const auto character : carven::runtime::text_chars(text)) {
            characters.push_back(character);
        }
        if (!ct::expect_equal(characters.size(), 4u)) {
            return;
        }
        ct::expect_equal(
            static_cast<std::uint32_t>(characters[0]),
            static_cast<std::uint32_t>(U'a')
        );
        ct::expect_equal(
            static_cast<std::uint32_t>(characters[1]),
            static_cast<std::uint32_t>(U'\u00e9')
        );
        ct::expect_equal(
            static_cast<std::uint32_t>(characters[2]),
            static_cast<std::uint32_t>(U'\u4f60')
        );
        ct::expect_equal(
            static_cast<std::uint32_t>(characters[3]),
            static_cast<std::uint32_t>(U'\U0001f600')
        );
    });

    ct::test("Runtime: empty text has no scalar to dereference", [] static noexcept {
        const auto empty = carven::runtime::text_chars("");
        ct::expect(!(empty.begin() != empty.end()));
    });

    ct::test("Runtime: checked UTF-8 preserves the borrowed byte range", [] static noexcept {
        const auto text = std::string_view("a\0\xc3\xa9", 4);
        const auto checked = carven::runtime::checked_utf8(text);
        ct::expect(checked.data() == text.data());
        ct::expect(checked.size() == text.size());
        ct::expect(carven::runtime::checked_utf8({}).empty());
    });

    ct::test(
        "Runtime: UTF representation conversion preserves borrowed byte storage",
        [] static noexcept {
            const auto bytes = std::array<std::uint8_t, 3> {65, 0, 66};
            const auto input = carven::runtime::Slice<std::uint8_t>(std::span(bytes));
            const auto text = carven::runtime::utf8_text(input);
            ct::expect_equal(text, std::string_view("A\0B", 3));
            ct::expect(text.data() == reinterpret_cast<const char*>(bytes.data()));
        }
    );

    ct::test("Runtime: String byte views borrow the owner's storage", [] static noexcept {
        const auto text = carven::runtime::String::from_str("owning text with retained backing");
        const auto bytes = carven::runtime::text_bytes(text);
        ct::expect(bytes.data() == reinterpret_cast<const std::uint8_t*>(text.as_str().data()));
        ct::expect(bytes.size() == text.size());
    });
});

} // namespace
