module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/writer.hpp>

module carven:test.internal.runtime.writer;

import :test.internal.harness.death;
import std;

namespace {

template<int Base, bool Uppercase, bool ZeroPad, typename Integer>
auto check_integer(Integer value, std::size_t width) noexcept -> void {
    const auto presentation = Base == 2 ? (Uppercase ? 'B' : 'b')
        : Base == 8                     ? 'o'
        : Base == 16                    ? (Uppercase ? 'X' : 'x')
                                        : 'd';
    auto specification = std::string("{:");
    if constexpr (ZeroPad) {
        specification.push_back('0');
    }
    if (width != 0uz) {
        specification += std::to_string(width);
    }
    specification.push_back(presentation);
    specification.push_back('}');
    const auto expected =
        std::string("我\0", 4uz) + std::vformat(specification, std::make_format_args(value)) + "!";
    auto output = carven::runtime::String::from_str(std::string_view("我\0", 4uz));
    const auto size = expected.size() - output.size();
    auto writer = carven::runtime::Writer(output, size, size);
    writer.integer<Base, Uppercase, ZeroPad>(value, width);
    writer.append("!");
    CHECK(output.as_str() == expected);
}

template<int Base, bool Uppercase, bool ZeroPad>
auto check_policy() noexcept -> void {
    const auto widths = std::array {0uz, 1uz, 8uz, 64uz, 128uz};
    for (const auto width : widths) {
        for (const auto value :
             {0ll,
              -1ll,
              1ll,
              std::numeric_limits<long long>::min(),
              std::numeric_limits<long long>::max()}) {
            check_integer<Base, Uppercase, ZeroPad>(value, width);
        }
        check_integer<Base, Uppercase, ZeroPad>(std::numeric_limits<std::uint64_t>::max(), width);
        check_integer<Base, Uppercase, ZeroPad>(std::int8_t {-128}, width);
        check_integer<Base, Uppercase, ZeroPad>(std::uint8_t {255}, width);
    }
}

template<int Base>
auto check_base() noexcept -> void {
    check_policy<Base, false, false>();
    check_policy<Base, false, true>();
    if constexpr (Base == 16) {
        check_policy<Base, true, false>();
        check_policy<Base, true, true>();
    }
}

} // namespace

TEST_CASE("Runtime Writer: integer boundaries and padding match the native formatter") {
    check_base<2>();
    check_base<8>();
    check_base<10>();
    check_base<16>();
    check_integer<10, false, true>(-7, 65537uz);
}

TEST_CASE("Runtime Writer: repeated writes retain owners across growth") {
    auto output = carven::runtime::String::from_str("prefix:");
    auto writer = carven::runtime::Writer(output, 0uz, std::numeric_limits<std::size_t>::max());
    writer.integer<16, true, true>(42u, 16uz);
    writer.append(std::string_view("\0我", 4uz));
    const auto saved = output;
    writer.integer<10, false, false>(-7, 4096uz);
    CHECK(saved.as_str() == std::string_view("prefix:000000000000002A\0我", 27uz));
    CHECK(output.size() == saved.size() + 4096uz);
    CHECK(output.as_str().ends_with("-7"));
    CHECK(saved.as_str().data() != output.as_str().data());
}

TEST_CASE("Runtime Writer: unrepresentable destination capacity terminates") {
    CHECK(expect_termination("writer-size", []() static noexcept {
        auto output = carven::runtime::String::from_str("prefix");
        const auto size = std::numeric_limits<std::size_t>::max();
        auto writer = carven::runtime::Writer(output, size, size);
        writer.append("unused");
    }));
}

TEST_CASE("Runtime Writer: an upper bound alone does not require its storage") {
    auto output = carven::runtime::String::from_str("value=");
    auto writer = carven::runtime::Writer(output, 0uz, std::numeric_limits<std::size_t>::max());
    writer.integer<2, false, false>(7u, 0uz);
    CHECK(output.as_str() == "value=111");
}

TEST_CASE("Runtime writer: mixed fields size completed text and preserve independent UTF-8 bytes") {
    const auto input = carven::runtime::String::from_str(std::string(4096uz, 'x') + "我");
    const auto view = std::string_view("a\0b", 3uz);
    auto output = carven::runtime::String::from_str("prefix:");
    auto writer = carven::runtime::Writer(output, 7uz, 11uz, {input.size(), view.size()});
    const auto* allocation = output.as_str().data();
    writer.append(input);
    writer.append(view);
    writer.boolean(false);
    writer.character(U'😀');
    writer.integer<16, true, true>(std::uint8_t {255}, 2uz);
    CHECK(
        output.as_str()
        == std::string("prefix:") + std::string(input.as_str()) + std::string(view) + "false😀FF"
    );
    CHECK(output.as_str().data() == allocation);
    CHECK(input.size() == 4099uz);
}

TEST_CASE("Runtime Writer: cumulative text lengths reject overflow before allocation") {
    CHECK(expect_termination("writer-text-sizes", []() static noexcept {
        auto output = carven::runtime::String();
        const auto maximum = std::string().max_size();
        auto writer = carven::runtime::Writer(output, 0uz, 0uz, {maximum, 1uz});
        writer.append("unused");
    }));
}

TEST_CASE("Runtime Writer: saturated text upper bounds preserve available storage") {
    auto output = carven::runtime::String::from_str("prefix:");
    const auto* storage = output.as_str().data();
    auto writer =
        carven::runtime::Writer(output, 0uz, std::numeric_limits<std::size_t>::max(), {1uz, 1uz});
    writer.append("ab");
    CHECK(output.as_str() == "prefix:ab");
    CHECK(output.as_str().data() == storage);
}

TEST_CASE("Writer: floating conversions preserve native formatting across precisions") {
    const auto check = []<typename Float>(Float value) static noexcept {
        auto output = carven::runtime::String();
        auto writer = carven::runtime::Writer(output, 0uz, 2048uz);
        writer.floating(value);
        writer.append("/");
        writer.fixed<2>(value);
        writer.append("/");
        writer.scientific<6>(value);
        writer.append("/");
        writer.general<0>(value);
        writer.append("/");
        writer.fixed<256>(value);
        CHECK(
            output.as_str()
            == std::format("{}/{:.2f}/{:.6e}/{:.0g}/{:.256f}", value, value, value, value, value)
        );
    };
    for (const auto value :
         {0.0,
          -0.0,
          1.25,
          -42.5,
          1.0e20,
          1.0e-12,
          std::numeric_limits<double>::max(),
          std::numeric_limits<double>::denorm_min(),
          std::numeric_limits<double>::infinity(),
          -std::numeric_limits<double>::infinity(),
          std::numeric_limits<double>::quiet_NaN()}) {
        check(value);
    }
    for (const auto value :
         {0.0f,
          -0.0f,
          1.25f,
          std::numeric_limits<float>::max(),
          std::numeric_limits<float>::denorm_min()}) {
        check(value);
    }
}
