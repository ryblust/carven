module;
#include <carven/runtime/format.hpp>

module carven:test.internal.runtime.format;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

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

const ct::Suite tests([] static noexcept {
    ct::test("Runtime: formatting owns validated UTF-8 including NUL", [] static noexcept {
        const auto text = carven::runtime::String::from_str("é我😀");
        ct::expect(carven::runtime::format("{} {}", text, U'😀').as_str() == "é我😀 😀");
        ct::expect(
            carven::runtime::format(std::string_view("a\0{0}", 5), 7).as_str()
            == std::string_view(
                "a\0"
                "7",
                3
            )
        );
        ct::expect(
            carven::runtime::format("{{}} {:04x} {:.2f}", 42, 1.25).as_str() == "{} 002a 1.25"
        );
        ct::expect(carven::runtime::format("{:>{}}", U'我', 4).as_str() == "  我");
        ct::expect(carven::runtime::format("{0:{1}.{2}f}", 1.25, 7, 1).as_str() == "    1.2");
    });

    ct::test(
        "Runtime: proven UTF-8 formatting preserves builtin bytes and NUL",
        [] static noexcept {
            const auto text = carven::runtime::String::from_str("é我😀");
            ct::expect(
                carven::runtime::format_valid_utf8("{} {} {} {:08x}", text, U'😀', true, 42)
                    .as_str()
                == carven::runtime::format("{} {} {} {:08x}", text, U'😀', true, 42).as_str()
            );
            ct::expect(
                carven::runtime::format_valid_utf8(std::string_view("a\0{0}", 5), 7).as_str()
                == std::string_view(
                    "a\0"
                    "7",
                    3
                )
            );
            ct::expect(carven::runtime::format_valid_utf8("{{}} {:04x}", 42).as_str() == "{} 002a");
            ct::expect(
                carven::runtime::format_valid_utf8("{} {}", U'\0', false).as_str()
                == std::string_view("\0 false", 7)
            );
        }
    );

    ct::test("Runtime: proven UTF-8 formatting owns independent long storage", [] static noexcept {
        auto bytes = std::string(8192uz, 'x');
        bytes.replace(1024uz, 8uz, std::string_view("我\0😀", 8uz));
        auto input = carven::runtime::String::from_str(bytes);
        auto result = carven::runtime::format_valid_utf8("[{}]", input);
        const auto checked = carven::runtime::format("[{}]", input);
        const auto copied = result;
        ct::expect(result == checked);
        ct::expect(result.as_str().data() != input.as_str().data());
        ct::expect(result.as_str().data() != copied.as_str().data());
        input.clear();
        result.append("!");
        ct::expect(copied == checked);
        ct::expect(result.size() == copied.size() + 1uz);
        ct::expect(copied.as_str().substr(1025uz, 8uz) == std::string_view("我\0😀", 8uz));
    });

    ct::test(
        "Runtime: general formatting still rejects invalid UTF-8 character output",
        [] static noexcept {
            ct::expect(expect_termination(
                "runtime-format-integer-character-invalid-utf8",
                []() static noexcept {
                    constexpr auto value = std::numeric_limits<char>::is_signed ? -61 : 195;
                    static_cast<void>(carven::runtime::format("{:c}", value));
                }
            ));
        }
    );

    ct::test(
        "Runtime: formatted append preserves existing text and formatted bytes",
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
            ct::expect(checked.as_str() == "prefix:é我😀 😀 true 0000002a");
            ct::expect(proven == checked);

            carven::runtime::append_format(checked, std::string_view("\0{0}", 4), U'\0');
            carven::runtime::append_format_valid_utf8(proven, std::string_view("\0{0}", 4), U'\0');
            ct::expect(proven == checked);
            ct::expect(
                proven.as_str().substr(proven.size() - 2uz) == std::string_view("\0\0", 2uz)
            );

            const auto before_empty = proven;
            carven::runtime::append_format(checked, "");
            carven::runtime::append_format_valid_utf8(proven, "");
            ct::expect(proven == before_empty);
            ct::expect(checked == before_empty);
        }
    );

    ct::test(
        "Runtime: formatted append owns bytes across growth and source mutation",
        [] static noexcept {
            auto source = carven::runtime::String::from_str(std::string(4096uz, 'x'));
            source.append("我");
            auto destination = carven::runtime::String::from_str("prefix");
            auto expected = std::string("prefix");
            for (auto index = 0; index < 4; ++index) {
                carven::runtime::append_format_valid_utf8(destination, "[{}:{}]", index, source);
                expected.append(std::format("[{}:{}]", index, source));
            }
            ct::expect(destination.as_str() == expected);
            source.clear();
            ct::expect(destination.as_str() == expected);
            const auto copy = destination;
            carven::runtime::append_format_valid_utf8(destination, "{}", U'😀');
            ct::expect(copy.as_str() == expected);
            ct::expect(destination.as_str() == expected + "😀");
        }
    );

    ct::test(
        "Runtime: proven formatted append retains available destination storage",
        [] static noexcept {
            auto destination = carven::runtime::String::from_str(std::string(8192uz, 'x'));
            destination.clear();
            const auto* allocation = destination.as_str().data();
            carven::runtime::append_format_valid_utf8(destination, "[{:04x}] {}", 42, U'我');
            ct::expect(destination.as_str() == "[002a] 我");
            ct::expect(destination.as_str().data() == allocation);
        }
    );

    ct::test(
        "Runtime: checked formatted append supports general specifications",
        [] static noexcept {
            auto destination = carven::runtime::String::from_str("prefix:");
            carven::runtime::append_format(destination, "{:>{}} {:.2f} {:c}", U'我', 4, 1.25, 65);
            ct::expect(destination.as_str() == "prefix:  我 1.25 A");
        }
    );

    ct::test("Runtime: checked formatted append rejects invalid UTF-8 output", [] static noexcept {
        ct::expect(expect_termination(
            "runtime-append-format-integer-character-invalid-utf8",
            []() static noexcept {
                auto destination = carven::runtime::String::from_str("prefix:");
                constexpr auto value = std::numeric_limits<char>::is_signed ? -61 : 195;
                carven::runtime::append_format(destination, "{:c}", value);
            }
        ));
    });
});

} // namespace
