module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/string.hpp>
#include <carven/runtime/passing.hpp>

module carven:test.internal.runtime.string;

import std;

TEST_CASE("Runtime: owning String preserves UTF-8 and independent copies") {
    const auto inputs = std::array {
        std::string_view(),
        std::string_view("a\0b", 3),
        std::string_view("é我😀é﻿"),
        std::string_view("long text that exceeds any ordinary small string buffer")
    };
    for (const auto input : inputs) {
        const auto owner = carven::runtime::String::from_str(input);
        auto copy = owner;
        copy.push(U'!');
        CHECK(owner.as_str() == input);
        CHECK(copy.size() == owner.size() + 1uz);
        CHECK(carven::runtime::utf8_is_valid(copy.as_str()));
        copy.clear();
        CHECK(copy.empty());
    }
    static_assert(std::same_as<
                  carven::runtime::ReadArg<carven::runtime::String>,
                  const carven::runtime::String&>);
    static_assert(!std::is_convertible_v<std::string, carven::runtime::String>);
    static_assert(!std::is_convertible_v<carven::runtime::String, std::string_view>);
}

TEST_CASE("Runtime: String push encodes every UTF-8 width and scalar boundary") {
    const auto scalars = std::array {
        U'\0',
        U'\x7f',
        U'\x80',
        U'\x7ff',
        U'\x800',
        U'\xd7ff',
        U'\xe000',
        U'\xffff',
        U'\x10000',
        U'\x10ffff'
    };
    auto text = carven::runtime::String();
    for (const auto scalar : scalars) {
        text.push(scalar);
    }
    CHECK(carven::runtime::utf8_is_valid(text.as_str()));
    auto decoded = std::vector<char32_t>();
    for (const auto scalar : carven::runtime::str_chars(text.as_str())) {
        decoded.push_back(scalar);
    }
    CHECK(std::ranges::equal(decoded, scalars));
    const auto moved = std::move(text);
    CHECK(moved.size() == 26uz);
}

TEST_CASE("Runtime: String operations can execute during constant evaluation") {
    static_assert([]() static noexcept {
        auto value = carven::runtime::String::from_str("a");
        value.push(U'我');
        auto copy = value;
        value.clear();
        auto moved = std::move(copy);
        moved.append("!");
        value = moved;
        return value == moved && value.size() == 5uz;
    }());
}
