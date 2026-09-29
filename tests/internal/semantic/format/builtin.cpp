module carven:test.internal.semantic.format.builtin;

import :semantic.format.builtin;
import :semantic.semir.constant;
import :semantic.semir.format;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Builtin formatting: integer bytes agree with native standard formatting",
        [] static noexcept {
            const auto numbers = std::to_array<std::int64_t>({
                0,
                1,
                -1,
                42,
                -42,
                std::numeric_limits<std::int64_t>::min(),
                std::numeric_limits<std::int64_t>::max(),
            });
            const auto specifications = std::to_array<std::string_view>({
                "",
                "d",
                "0",
                "04",
                "24",
                "024",
                "b",
                "B",
                "o",
                "x",
                "X",
                "080b",
                "024X",
            });
            for (auto value : numbers) {
                const auto constant = IntegerConstant::from_signed(value);
                ct::each(
                    specifications,
                    std::identity {},
                    [&](std::string_view specification) noexcept {
                        const auto actual = format_builtin_value(constant, specification, 1024uz);
                        if (!ct::expect(actual.has_value()).note("value = ", value)) {
                            return;
                        }
                        const auto format = std::format("{{:{}}}", specification);
                        ct::expect(*actual == std::vformat(format, std::make_format_args(value)))
                            .note("value = ", value);
                    }
                );
            }
            auto maximum = std::numeric_limits<std::uint64_t>::max();
            const auto constant = IntegerConstant::from_parts(maximum, false);
            ct::each(
                specifications,
                std::identity {},
                [&](std::string_view specification) noexcept {
                    const auto actual = format_builtin_value(constant, specification, 1024uz);
                    if (!ct::expect(actual.has_value())) {
                        return;
                    }
                    const auto format = std::format("{{:{}}}", specification);
                    ct::expect(*actual == std::vformat(format, std::make_format_args(maximum)));
                }
            );
        }
    );

    ct::test(
        "Builtin formatting: supported values respect byte budgets and unsupported inputs decline",
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                BuiltinFormatValue value;
                std::string_view text;
            };

            const auto embedded = std::string_view("a\0我", 5uz);
            const auto scenarios = std::array {
                Scenario {
                    .name = "false boolean",
                    .value = BooleanConstant {.value = false},
                    .text = "false"
                },
                Scenario {
                    .name = "unicode character",
                    .value = CharacterConstant {.scalar = U'我'},
                    .text = "我"
                },
                Scenario {.name = "embedded NUL text", .value = embedded, .text = embedded},
                Scenario {.name = "empty text", .value = std::string_view {}, .text = ""},
            };
            ct::each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
                const auto formatted =
                    format_builtin_value(scenario.value, {}, scenario.text.size());
                if (!ct::expect(formatted.has_value())) {
                    return;
                }
                ct::expect(*formatted == scenario.text);
                if (!scenario.text.empty()) {
                    const auto limited =
                        format_builtin_value(scenario.value, {}, scenario.text.size() - 1uz);
                    if (!ct::expect(!limited.has_value())) {
                        return;
                    }
                    ct::expect(limited.error() == BuiltinFormatFailureKind::Limit);
                }
                const auto unsupported = format_builtin_value(scenario.value, "x", 128uz);
                if (!ct::expect(!unsupported.has_value())) {
                    return;
                }
                ct::expect(unsupported.error() == BuiltinFormatFailureKind::Unsupported);
            });
            const auto unavailable = format_builtin_value(BuiltinFormatValue {}, {}, 128uz);
            if (!ct::expect(!(unavailable.has_value()))) {
                return;
            }
            ct::expect(unavailable.error() == BuiltinFormatFailureKind::Unsupported);
        }
    );

    ct::test(
        "Builtin formatting: floating policies match native output and bound work",
        [] static noexcept {
            const auto check = []<typename Float>(Float value) static noexcept {
                const auto constant = [&]() noexcept -> BuiltinFormatValue {
                    if constexpr (std::same_as<Float, float>) {
                        return F32Constant {.value = value};
                    } else {
                        return F64Constant {.value = value};
                    }
                }();
                static constexpr auto specifications = std::to_array<std::string_view>(
                    {"",
                     "f",
                     ".0f",
                     ".2f",
                     ".17g",
                     ".4e",
                     "a",
                     ".3A",
                     "+012.2f",
                     "#g",
                     "*>12.3G",
                     "我^12.2f",
                     " .2",
                     ".0g"}
                );
                ct::each(
                    specifications,
                    std::identity {},
                    [&](std::string_view specification) noexcept {
                        const auto expected = std::vformat(
                            std::format("{{:{}}}", specification),
                            std::make_format_args(value)
                        );
                        const auto actual = format_builtin_value(constant, specification, 2048uz);
                        if (!ct::expect(actual.has_value()).note("value = ", value)) {
                            return;
                        }
                        ct::expect(*actual == expected).note("value = ", value);
                        const auto limited =
                            format_builtin_value(constant, specification, expected.size() - 1uz);
                        if (!ct::expect(!limited.has_value()).note("value = ", value)) {
                            return;
                        }
                        ct::expect(limited.error() == BuiltinFormatFailureKind::Limit)
                            .note("value = ", value);
                    }
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
                  std::numeric_limits<float>::denorm_min(),
                  std::numeric_limits<float>::infinity()}) {
                check(value);
            }
            const auto invalid_cases = std::to_array<std::string_view>(
                {"x", "00", ".", "..2f", "2.3.4f", "{}", ".{}f", "{>5", "L", "+-f"}
            );
            ct::each(invalid_cases, std::identity {}, [](std::string_view invalid) static noexcept {
                const auto result =
                    format_builtin_value(F64Constant {.value = 1.0}, invalid, 1024uz);
                if (!ct::expect(!result.has_value())) {
                    return;
                }
                ct::expect(result.error() == BuiltinFormatFailureKind::Unsupported);
            });
            const auto oversized_cases = std::to_array<std::string_view>(
                {"999999999999999999999999f", ".99999999999999999999999f", ".2048f"}
            );
            ct::each(
                oversized_cases,
                std::identity {},
                [](std::string_view oversized) static noexcept {
                    const auto result =
                        format_builtin_value(F64Constant {.value = 1.0}, oversized, 1024uz);
                    if (!ct::expect(!(result.has_value()))) {
                        return;
                    }
                    ct::expect(result.error() == BuiltinFormatFailureKind::Limit);
                }
            );
        }
    );

    ct::test(
        "Builtin formatting: dynamic integer widths preserve zero and budget boundaries",
        [] static noexcept {
            const auto specification = FormatSpec {
                .parts = {{FormatHole {
                    .operand_index = 0uz,
                    .has_specification = true,
                    .specification = {{FormatHole {
                        .operand_index = 1uz,
                        .has_specification = false,
                        .specification = {}
                    }}}
                }}}
            };
            const auto widths = std::array {0, 1, 4};
            ct::each(
                widths,
                [](int width) static noexcept { return std::format("width {}", width); },
                [&](int width) noexcept {
                    const auto arguments = std::array<BuiltinFormatValue, 2> {
                        IntegerConstant::from_signed(1),
                        IntegerConstant::from_signed(width)
                    };
                    const auto expected = std::string(width > 1 ? width - 1 : 0, ' ') + "1";
                    const auto result = format_builtin(specification, arguments, expected.size());
                    if (!(ct::expect(result.has_value()))) {
                        return;
                    }
                    ct::expect(*result == expected);
                    const auto limited =
                        format_builtin(specification, arguments, expected.size() - 1uz);
                    if (!(ct::expect(!(limited.has_value())))) {
                        return;
                    }
                    ct::expect(limited.error().kind == BuiltinFormatFailureKind::Limit);
                }
            );
            const auto arguments = std::array<BuiltinFormatValue, 2> {
                BooleanConstant {.value = true},
                IntegerConstant::from_signed(0)
            };
            const auto unsupported = format_builtin(specification, arguments, 16uz);
            if (!ct::expect(!(unsupported.has_value()))) {
                return;
            }
            ct::expect(unsupported.error().kind == BuiltinFormatFailureKind::Unsupported);
        }
    );
});

} // namespace
