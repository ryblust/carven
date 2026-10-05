module;
#include <carven/runtime/string.hpp>
#include <carven/runtime/text.hpp>
#include <carven/runtime/passing.hpp>

module carven:test.internal.runtime.string;

import :support.quote;
import :test.harness.framework;
import std;

namespace {

// String ownership operations remain available during constant evaluation.
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


const TestSuite suite([] static noexcept {
    "Runtime: owning String preserves UTF-8 and independent copies"_test = [] static noexcept {
        const auto inputs = std::array {
            std::string_view(),
            std::string_view("a\0b", 3),
            std::string_view("é我😀é﻿"),
            std::string_view("long text that exceeds any ordinary small string buffer")
        };
        each(
            inputs,
            [](std::string_view input) static noexcept -> std::string { return quote_text(input); },
            [](std::string_view input) static noexcept {
                const auto owner = carven::runtime::String::from_str(input);
                auto copy = owner;
                copy.push(U'!');
                expect_equal(owner.as_str(), input);
                expect(copy.size() == owner.size() + 1uz);
                expect(carven::runtime::utf8_is_valid(copy.as_str()));
                copy.clear();
                expect(copy.empty());
            }
        );
        static_assert(std::same_as<
                      carven::runtime::ReadArg<carven::runtime::String>,
                      const carven::runtime::String&>);
        static_assert(!std::is_convertible_v<std::string, carven::runtime::String>);
        static_assert(!std::is_convertible_v<carven::runtime::String, std::string_view>);
    };

    "Runtime: String push encodes every UTF-8 width and scalar boundary"_test = [] static noexcept {
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
        expect(carven::runtime::utf8_is_valid(text.as_str()));
        auto decoded = std::vector<char32_t>();
        for (const auto scalar : carven::runtime::text_chars(text)) {
            decoded.push_back(scalar);
        }
        expect_range_equal(decoded, scalars);
        const auto moved = std::move(text);
        expect(moved.size() == 26uz);
    };

    "Runtime String: validated native storage is adopted"_test = [] static noexcept {
        auto bytes = std::string(8192uz, 'x');
        bytes.replace(1024uz, 8uz, std::string_view("我\0😀", 8uz));
        const auto* allocation = bytes.data();
        const auto result = carven::runtime::String::from_utf8(std::move(bytes));
        expect(result.as_str().data() == allocation);
        expect(result.size() == 8192uz);
        expect(result.as_str().substr(1024uz, 8uz) == std::string_view("我\0😀", 8uz));
    };
});

} // namespace
