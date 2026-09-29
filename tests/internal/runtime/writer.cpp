module;
#include <carven/runtime/writer.hpp>

module carven:test.internal.runtime.writer;

import :test.harness.framework;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

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
    ct::expect_equal(output.as_str(), expected).note([&] noexcept {
        return std::format("format={}, value={}, width={}", specification, +value, width);
    });
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

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Runtime Writer: integer boundaries and padding match the native formatter",
        [] static noexcept {
            check_base<2>();
            check_base<8>();
            check_base<10>();
            check_base<16>();
            check_integer<10, false, true>(-7, 65537uz);
        }
    );

    ct::test("Runtime Writer: repeated writes retain owners across growth", [] static noexcept {
        auto output = carven::runtime::String::from_str("prefix:");
        auto writer = carven::runtime::Writer(output, 0uz, std::numeric_limits<std::size_t>::max());
        writer.integer<16, true, true>(42u, 16uz);
        writer.append(std::string_view("\0我", 4uz));
        const auto saved = output;
        writer.integer<10, false, false>(-7, 4096uz);
        ct::expect_equal(saved.as_str(), std::string_view("prefix:000000000000002A\0我", 27uz));
        ct::expect_equal(output.size(), saved.size() + 4096uz);
        ct::expect(output.as_str().ends_with("-7"));
        ct::expect(saved.as_str().data() != output.as_str().data());
    });

    ct::test("Runtime Writer: unrepresentable destination capacity terminates", [] static noexcept {
        ct::expect(expect_termination("writer-size", []() static noexcept {
            auto output = carven::runtime::String::from_str("prefix");
            const auto size = std::numeric_limits<std::size_t>::max();
            auto writer = carven::runtime::Writer(output, size, size);
            writer.append("unused");
        }));
    });

    ct::test(
        "Runtime Writer: an upper bound alone does not require its storage",
        [] static noexcept {
            auto output = carven::runtime::String::from_str("value=");
            auto writer =
                carven::runtime::Writer(output, 0uz, std::numeric_limits<std::size_t>::max());
            writer.integer<2, false, false>(7u, 0uz);
            ct::expect_equal(output.as_str(), "value=111");
        }
    );

    ct::test(
        "Runtime Writer: mixed fields size completed text and preserve independent UTF-8 bytes",
        [] static noexcept {
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
            ct::expect_equal(
                output.as_str(),
                std::string("prefix:") + std::string(input.as_str()) + std::string(view)
                    + "false😀FF"
            );
            ct::expect(output.as_str().data() == allocation);
            ct::expect_equal(input.size(), 4099uz);
        }
    );

    ct::test(
        "Runtime Writer: cumulative text lengths reject overflow before allocation",
        [] static noexcept {
            ct::expect(expect_termination("writer-text-sizes", []() static noexcept {
                auto output = carven::runtime::String();
                const auto maximum = std::string().max_size();
                auto writer = carven::runtime::Writer(output, 0uz, 0uz, {maximum, 1uz});
                writer.append("unused");
            }));
        }
    );

    ct::test(
        "Runtime Writer: saturated text upper bounds preserve available storage",
        [] static noexcept {
            auto output = carven::runtime::String::from_str("prefix:");
            const auto* storage = output.as_str().data();
            auto writer = carven::runtime::Writer(
                output,
                0uz,
                std::numeric_limits<std::size_t>::max(),
                {1uz, 1uz}
            );
            writer.append("ab");
            ct::expect_equal(output.as_str(), "prefix:ab");
            ct::expect(output.as_str().data() == storage);
        }
    );

    ct::test(
        "Runtime Writer: floating conversions preserve native formatting across precisions",
        [] static noexcept {
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
                ct::expect_equal(
                    output.as_str(),
                    std::format(
                        "{}/{:.2f}/{:.6e}/{:.0g}/{:.256f}",
                        value,
                        value,
                        value,
                        value,
                        value
                    )
                )
                    .note("value =", value);
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
    );

    ct::test("Runtime Writer: dynamic integer widths match native formatting", [] static noexcept {
        const auto check =
            []<typename Value, typename Width>(Value value, Width width) static noexcept {
                auto output = carven::runtime::String::from_str("prefix:");
                auto writer = carven::runtime::Writer(output, 0uz, 0uz);
                writer.integer_dynamic_width<2, false, false>(value, width);
                writer.append("/");
                writer.integer_dynamic_width<2, true, true>(value, width);
                writer.append("/");
                writer.integer_dynamic_width<8, false, false>(value, width);
                writer.append("/");
                writer.integer_dynamic_width<10, false, true>(value, width);
                writer.append("/");
                writer.integer_dynamic_width<16, false, true>(value, width);
                writer.append("/");
                writer.integer_dynamic_width<16, true, false>(value, width);
                ct::expect_equal(
                    output.as_str(),
                    std::format(
                        "prefix:{0:{1}b}/{0:0{1}B}/{0:{1}o}/{0:0{1}d}/{0:0{1}x}/{0:{1}X}",
                        value,
                        width
                    )
                )
                    .note([&] noexcept {
                        return std::format("width={}, value={}", width, +value);
                    });
            };
        for (const auto width : {0, 1, 8, 80}) {
            for (const auto value :
                 {0ll,
                  -1ll,
                  std::numeric_limits<long long>::min(),
                  std::numeric_limits<long long>::max()}) {
                check(value, width);
            }
            check(std::numeric_limits<unsigned long long>::max(), width);
            check(std::int8_t {-128}, static_cast<short>(width));
        }
    });

    ct::test("Runtime Writer: negative dynamic widths terminate", [] static noexcept {
        ct::expect(expect_termination("writer-negative-width", []() static noexcept {
            auto output = carven::runtime::String();
            auto writer = carven::runtime::Writer(output, 0uz, 0uz);
            writer.integer_dynamic_width<16, true, true>(-7, -1);
        }));
    });
});

} // namespace
