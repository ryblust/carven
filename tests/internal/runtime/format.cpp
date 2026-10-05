module;
#include <carven/runtime/format.hpp>

module carven:test.internal.runtime.format;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

constexpr auto integer_format = std::format_string<const int&>("{}");
constexpr auto scalar_format = std::format_string<carven::runtime::String, const bool&>("{} {}");

} // namespace

static_assert(noexcept(carven::runtime::format(integer_format, 7)));
static_assert(noexcept(carven::runtime::format_valid_utf8(integer_format, 7)));
static_assert(noexcept(carven::runtime::format_valid_utf8(scalar_format, U'我', true)));
static_assert(noexcept(
    carven::runtime::append_format(std::declval<carven::runtime::String&>(), integer_format, 7)
));
static_assert(noexcept(carven::runtime::append_format_valid_utf8(
    std::declval<carven::runtime::String&>(),
    scalar_format,
    U'我',
    true
)));
static_assert(!std::constructible_from<carven::runtime::String, std::string&&>);

namespace {

const TestSuite suite([] static noexcept {
    "Runtime: formatting owns validated UTF-8 including NUL"_test = [] static noexcept {
        const auto text = carven::runtime::String::from_str("é我😀");
        expect(carven::runtime::format("{} {}", text, U'😀').as_str() == "é我😀 😀");
        expect(
            carven::runtime::format(std::string_view("a\0{0}", 5), 7).as_str()
            == std::string_view(
                "a\0"
                "7",
                3
            )
        );
        expect(carven::runtime::format("{{}} {:04x} {:.2f}", 42, 1.25).as_str() == "{} 002a 1.25");
        expect(carven::runtime::format("{:>{}}", U'我', 4).as_str() == "  我");
        expect(carven::runtime::format("{0:{1}.{2}f}", 1.25, 7, 1).as_str() == "    1.2");
    };

    "Runtime: proven UTF-8 formatting preserves builtin bytes and NUL"_test = [] static noexcept {
        const auto text = carven::runtime::String::from_str("é我😀");
        expect(
            carven::runtime::format_valid_utf8("{} {} {} {:08x}", text, U'😀', true, 42).as_str()
            == carven::runtime::format("{} {} {} {:08x}", text, U'😀', true, 42).as_str()
        );
        expect(
            carven::runtime::format_valid_utf8(std::string_view("a\0{0}", 5), 7).as_str()
            == std::string_view(
                "a\0"
                "7",
                3
            )
        );
        expect(carven::runtime::format_valid_utf8("{{}} {:04x}", 42).as_str() == "{} 002a");
        expect(
            carven::runtime::format_valid_utf8("{} {}", U'\0', false).as_str()
            == std::string_view("\0 false", 7)
        );
    };

    "Runtime: proven UTF-8 formatting owns independent long storage"_test = [] static noexcept {
        auto bytes = std::string(8192uz, 'x');
        bytes.replace(1024uz, 8uz, std::string_view("我\0😀", 8uz));
        auto input = carven::runtime::String::from_str(bytes);
        auto result = carven::runtime::format_valid_utf8("[{}]", input);
        const auto checked = carven::runtime::format("[{}]", input);
        const auto copied = result;
        expect(result == checked);
        expect(result.as_str().data() != input.as_str().data());
        expect(result.as_str().data() != copied.as_str().data());
        input.clear();
        result.append("!");
        expect(copied == checked);
        expect(result.size() == copied.size() + 1uz);
        expect(copied.as_str().substr(1025uz, 8uz) == std::string_view("我\0😀", 8uz));
    };

    "Runtime: general formatting still rejects invalid UTF-8 character output"_test =
        [] static noexcept {
            expect(expect_termination(
                "runtime-format-integer-character-invalid-utf8",
                []() static noexcept {
                    constexpr auto value = std::numeric_limits<char>::is_signed ? -61 : 195;
                    static_cast<void>(carven::runtime::format("{:c}", value));
                }
            ));
        };

    "Runtime: formatted append preserves existing text and formatted bytes"_test =
        [] static noexcept {
            auto checked = carven::runtime::String::from_str("prefix:");
            auto proven = checked;
            const auto input = carven::runtime::String::from_str("é我😀");
            carven::runtime::append_format(checked, "{} {} {} {:08x}", input, U'😀', true, 42);
            carven::runtime::append_format_valid_utf8(
                proven,
                "{} {} {} {:08x}",
                input,
                U'😀',
                true,
                42
            );
            expect(checked.as_str() == "prefix:é我😀 😀 true 0000002a");
            expect(proven == checked);

            carven::runtime::append_format(checked, std::string_view("\0{0}", 4), U'\0');
            carven::runtime::append_format_valid_utf8(proven, std::string_view("\0{0}", 4), U'\0');
            expect(proven == checked);
            expect(proven.as_str().substr(proven.size() - 2uz) == std::string_view("\0\0", 2uz));

            const auto before_empty = proven;
            carven::runtime::append_format(checked, "");
            carven::runtime::append_format_valid_utf8(proven, "");
            expect(proven == before_empty);
            expect(checked == before_empty);
        };

    "Runtime: formatted append owns bytes across growth and source mutation"_test =
        [] static noexcept {
            auto source = carven::runtime::String::from_str(std::string(4096uz, 'x'));
            source.append("我");
            auto destination = carven::runtime::String::from_str("prefix");
            auto expected = std::string("prefix");
            for (auto index = 0; index < 4; ++index) {
                carven::runtime::append_format_valid_utf8(destination, "[{}:{}]", index, source);
                expected.append(std::format("[{}:{}]", index, source));
            }
            expect(destination.as_str() == expected);
            source.clear();
            expect(destination.as_str() == expected);
            const auto copy = destination;
            carven::runtime::append_format_valid_utf8(destination, "{}", U'😀');
            expect(copy.as_str() == expected);
            expect(destination.as_str() == expected + "😀");
        };

    "Runtime: proven formatted append retains available destination storage"_test =
        [] static noexcept {
            auto destination = carven::runtime::String::from_str(std::string(8192uz, 'x'));
            destination.clear();
            const auto* allocation = destination.as_str().data();
            carven::runtime::append_format_valid_utf8(destination, "[{:04x}] {}", 42, U'我');
            expect(destination.as_str() == "[002a] 我");
            expect(destination.as_str().data() == allocation);
        };

    "Runtime: checked formatted append supports general specifications"_test = [] static noexcept {
        auto destination = carven::runtime::String::from_str("prefix:");
        carven::runtime::append_format(destination, "{:>{}} {:.2f} {:c}", U'我', 4, 1.25, 65);
        expect(destination.as_str() == "prefix:  我 1.25 A");
    };

    "Runtime: checked formatted append rejects invalid UTF-8 output"_test = [] static noexcept {
        expect(expect_termination(
            "runtime-append-format-integer-character-invalid-utf8",
            []() static noexcept {
                auto destination = carven::runtime::String::from_str("prefix:");
                constexpr auto value = std::numeric_limits<char>::is_signed ? -61 : 195;
                carven::runtime::append_format(destination, "{:c}", value);
            }
        ));
    };
});

} // namespace
