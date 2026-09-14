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
