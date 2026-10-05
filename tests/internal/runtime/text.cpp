module;
#include <carven/runtime/text.hpp>

module carven:test.internal.runtime.text;

import :test.harness.framework;
import std;

namespace {

constexpr auto site = carven::runtime::SourceSite::native();

const TestSuite suite([] static noexcept {
    "Runtime: text views expose bytes and Unicode scalar values"_test = [] static noexcept {
        const auto text = std::string_view("a\xc3\xa9\xe4\xbd\xa0\xf0\x9f\x98\x80", 10);
        auto bytes = std::vector<std::uint8_t>();
        for (const auto byte : carven::runtime::text_bytes(text)) {
            bytes.push_back(byte);
        }
        if (!expect_equal(bytes.size(), 10u)) {
            return;
        }
        expect_equal(bytes.front(), 0x61u);
        expect_equal(bytes.back(), 0x80u);

        auto characters = std::vector<char32_t>();
        for (const auto character : carven::runtime::text_chars(text)) {
            characters.push_back(character);
        }
        if (!expect_equal(characters.size(), 4u)) {
            return;
        }
        expect_equal(static_cast<std::uint32_t>(characters[0]), static_cast<std::uint32_t>(U'a'));
        expect_equal(
            static_cast<std::uint32_t>(characters[1]),
            static_cast<std::uint32_t>(U'\u00e9')
        );
        expect_equal(
            static_cast<std::uint32_t>(characters[2]),
            static_cast<std::uint32_t>(U'\u4f60')
        );
        expect_equal(
            static_cast<std::uint32_t>(characters[3]),
            static_cast<std::uint32_t>(U'\U0001f600')
        );
    };

    "Runtime: empty text has no scalar to dereference"_test = [] static noexcept {
        const auto empty = carven::runtime::text_chars("");
        expect(!(empty.begin() != empty.end()));
    };

    "Runtime: checked UTF-8 preserves the borrowed byte range"_test = [] static noexcept {
        const auto text = std::string_view("a\0\xc3\xa9", 4);
        const auto checked = carven::runtime::checked_utf8(text, site);
        expect(checked.data() == text.data());
        expect(checked.size() == text.size());
        expect(carven::runtime::checked_utf8({}, site).empty());
    };

    "Runtime: UTF representation conversion preserves borrowed byte storage"_test =
        [] static noexcept {
            const auto bytes = std::array<std::uint8_t, 3> {65, 0, 66};
            const auto input = carven::runtime::Slice<std::uint8_t>(std::span(bytes));
            const auto text = carven::runtime::utf8_text(input);
            expect_equal(text, std::string_view("A\0B", 3));
            expect(text.data() == reinterpret_cast<const char*>(bytes.data()));
        };

    "Runtime: String byte views borrow the owner's storage"_test = [] static noexcept {
        const auto text = carven::runtime::String::from_str("owning text with retained backing");
        const auto bytes = carven::runtime::text_bytes(text);
        expect(bytes.data() == reinterpret_cast<const std::uint8_t*>(text.as_str().data()));
        expect(bytes.size() == text.size());
    };
});

} // namespace
