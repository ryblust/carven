module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/utf.hpp>

module carven:test.internal.runtime.utf;

import std;

TEST_CASE("Runtime: UTF-8 ingress validator rejects malformed scalar encodings") {
    const auto rejects = [](std::string_view bytes) static noexcept {
        return !carven::runtime::utf8_is_valid(bytes);
    };
    CHECK(carven::runtime::utf8_is_valid("plain"));
    CHECK(carven::runtime::utf8_is_valid("\xc3\xa9\xe4\xbd\xa0"));
    CHECK(rejects(std::string_view("\xc2", 1)));
    CHECK(rejects(std::string_view("\xc0\x80", 2)));
    CHECK(rejects(std::string_view("\xed\xa0\x80", 3)));
    CHECK(rejects(std::string_view("\xf4\x90\x80\x80", 4)));
    CHECK(rejects(std::string_view("\xe2\x28\xa1", 3)));
}

TEST_CASE("Runtime: UTF primitives encode and decode scalar boundaries") {
    const auto cases = std::array {
        std::pair {U'\0', std::string_view("\0", 1)},
        std::pair {U'\x7f', std::string_view("\x7f")},
        std::pair {U'\u0080', std::string_view("\xc2\x80")},
        std::pair {U'\u07ff', std::string_view("\xdf\xbf")},
        std::pair {U'\u0800', std::string_view("\xe0\xa0\x80")},
        std::pair {U'\uffff', std::string_view("\xef\xbf\xbf")},
        std::pair {U'\U00010000', std::string_view("\xf0\x90\x80\x80")},
        std::pair {U'\U0010ffff', std::string_view("\xf4\x8f\xbf\xbf")},
    };
    for (const auto& [scalar, bytes] : cases) {
        const auto encoded = carven::runtime::encode_valid_utf8(scalar);
        CHECK_EQ(std::string_view(encoded.bytes.data(), encoded.width), bytes);
        const auto decoded =
            carven::runtime::decode_valid_utf8(std::span(bytes.data(), bytes.size()));
        CHECK_EQ(decoded.scalar, scalar);
        CHECK_EQ(decoded.width, bytes.size());
        CHECK(carven::runtime::utf8_is_valid(bytes));
    }
}

TEST_CASE("Runtime: UTF representation conversion preserves borrowed byte storage") {
    const auto bytes = std::array<std::uint8_t, 3> {65, 0, 66};
    const auto input = carven::runtime::Slice<std::uint8_t>(std::span(bytes));
    const auto text = carven::runtime::utf8_text(input);
    CHECK_EQ(text, std::string_view("A\0B", 3));
    CHECK(text.data() == reinterpret_cast<const char*>(bytes.data()));
}
