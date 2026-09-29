module carven:test.internal.backend.preparation.classification;

import :backend.preparation.format;
import :semantic.format;
import :semantic.semir.type;
import :support.quote;
import :test.harness.framework;
import :test.internal.semantic.format.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Integer formatting: parsed fields retain literal bytes and exact type-derived sizes",
        [] static noexcept {
            const auto types = std::array<std::optional<BuiltinType>, 2> {
                BuiltinType::U32,
                BuiltinType::I64,
            };
            const auto parsed = classify_writer_format(
                FormatSpec {
                    .parts =
                        {format_text(std::string("{我}\0", 6uz)),
                         format_field(0uz, {format_text("08X")}),
                         format_text("/"),
                         format_field(1uz, {format_text("020d")})}
                },
                types
            );
            if (!ct::expect(parsed.has_value())) {
                return;
            }
            ct::expect(
                parsed->text == std::vector<std::string> {std::string("{我}\0", 6uz), "/", ""}
            );
            ct::expect(
                parsed->fields
                == std::vector<WriterFormatField> {
                    IntegerFormatField {
                        .base = 16,
                        .uppercase = true,
                        .zero_pad = true,
                        .static_width = 8u
                    },
                    IntegerFormatField {
                        .base = 10,
                        .uppercase = false,
                        .zero_pad = true,
                        .static_width = 20u
                    },
                }
            );
            ct::expect(parsed->minimum_size == 35u);
            ct::expect(parsed->maximum_size == 35u);
        }
    );

    ct::test(
        "Integer formatting: size bounds include every value and the sign of a signed minimum",
        [] static noexcept {
            struct Scenario final {
                BuiltinType type;
                FormatSpec format;
                std::uint64_t minimum_size;
                std::uint64_t maximum_size;
            };

            const auto scenarios = std::to_array<Scenario>({
                {.type = BuiltinType::U64,
                 .format =
                     FormatSpec {
                         .parts =
                             {format_text("value="),
                              format_field(0uz, {format_text("016X")}),
                              format_text(";")}
                     },
                 .minimum_size = 23u,
                 .maximum_size = 23u},
                {.type = BuiltinType::I64,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("016x")})}},
                 .minimum_size = 16u,
                 .maximum_size = 17u},
                {.type = BuiltinType::I64,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("017x")})}},
                 .minimum_size = 17u,
                 .maximum_size = 17u},
                {.type = BuiltinType::I8,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("04d")})}},
                 .minimum_size = 4u,
                 .maximum_size = 4u},
                {.type = BuiltinType::I8,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("03d")})}},
                 .minimum_size = 3u,
                 .maximum_size = 4u},
                {.type = BuiltinType::U8,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("08b")})}},
                 .minimum_size = 8u,
                 .maximum_size = 8u},
                {.type = BuiltinType::I8,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("08B")})}},
                 .minimum_size = 8u,
                 .maximum_size = 9u},
                {.type = BuiltinType::I8,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("09B")})}},
                 .minimum_size = 9u,
                 .maximum_size = 9u},
                {.type = BuiltinType::U64,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("22o")})}},
                 .minimum_size = 22u,
                 .maximum_size = 22u},
                {.type = BuiltinType::U64,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("o")})}},
                 .minimum_size = 1u,
                 .maximum_size = 22u},
                {.type = BuiltinType::I32,
                 .format = FormatSpec {.parts = {format_field(0uz, {format_text("65537")})}},
                 .minimum_size = 65537u,
                 .maximum_size = 65537u},
            });
            ct::each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string {
                    return quote_text(serialize_format(scenario.format));
                },
                [](const Scenario& scenario) static noexcept {
                    const auto types = std::array<std::optional<BuiltinType>, 1> {scenario.type};
                    const auto parsed = classify_writer_format(scenario.format, types);
                    if (!(ct::expect(parsed.has_value()).note([&] noexcept {
                            return std::format(
                                "serialize_format(scenario.format): {}",
                                serialize_format(scenario.format)
                            );
                        }))) {
                        return;
                    }
                    ct::expect(parsed->minimum_size == scenario.minimum_size).note([&] noexcept {
                        return std::format(
                            "serialize_format(scenario.format): {}",
                            serialize_format(scenario.format)
                        );
                    });
                    ct::expect(parsed->maximum_size == scenario.maximum_size).note([&] noexcept {
                        return std::format(
                            "serialize_format(scenario.format): {}",
                            serialize_format(scenario.format)
                        );
                    });
                }
            );
        }
    );

    ct::test(
        "Integer formatting: unknown types and unsupported or malformed specifications remain general",
        [] static noexcept {
            const auto integer = std::array<std::optional<BuiltinType>, 1> {BuiltinType::I32};
            for (const auto specification :
                 {"00", "+d", "#x", "c", "L", "2147483648", "999999999999999999999999"}) {
                ct::expect(
                    !(classify_writer_format(
                          FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}},
                          integer
                    )
                          .has_value())
                )
                    .note("specification: ", specification);
            }
            ct::expect(!(classify_writer_format(FormatSpec {.parts = {format_field(1uz)}}, integer)
                             .has_value()));
            ct::expect(!(classify_writer_format(
                             FormatSpec {.parts = {format_field(0uz), format_field(0uz)}},
                             integer
            )
                             .has_value()));
            ct::expect(!(classify_writer_format(
                             FormatSpec {.parts = {format_field(0uz, {format_field(1uz)})}},
                             integer
            )
                             .has_value()));
            for (const auto type :
                 {std::optional(BuiltinType::Void), std::optional<BuiltinType>()}) {
                const auto types = std::array {type};
                ct::expect(
                    !(classify_writer_format(FormatSpec {.parts = {format_field(0uz)}}, types)
                          .has_value())
                );
            }
        }
    );

    ct::test(
        "Writer formatting: mixed bounds exclude runtime text and include bool and Unicode scalars",
        [] static noexcept {
            const auto types = std::array<std::optional<BuiltinType>, 5> {
                BuiltinType::Str,
                BuiltinType::String,
                BuiltinType::Bool,
                BuiltinType::Char,
                BuiltinType::U8,
            };
            const auto parsed = classify_writer_format(
                FormatSpec {
                    .parts =
                        {format_text("["),
                         format_field(0uz),
                         format_field(1uz),
                         format_field(2uz),
                         format_field(3uz),
                         format_field(4uz, {format_text("02X")}),
                         format_text("]")}
                },
                types
            );
            if (!ct::expect(parsed.has_value())) {
                return;
            }
            ct::expect(parsed->minimum_size == 9u);
            ct::expect(parsed->maximum_size == 13u);
            ct::expect(parsed->fields.size() == 5uz);
            for (const auto type :
                 {BuiltinType::Str, BuiltinType::String, BuiltinType::Bool, BuiltinType::Char}) {
                const auto operand = std::array<std::optional<BuiltinType>, 1> {type};
                ct::expect(!(classify_writer_format(
                                 FormatSpec {.parts = {format_field(0uz, {format_text(">8")})}},
                                 operand
                )
                                 .has_value()));
            }
        }
    );

    ct::test(
        "Floating formatting: direct policies bound output and delegate decorated or large formats",
        [] static noexcept {
            const auto types = std::array<std::optional<BuiltinType>, 1uz> {BuiltinType::F64};
            for (const auto specification : {"", "f", ".2f", ".4e", ".17g", ".0", ".256f"}) {
                const auto format =
                    FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}};
                const auto prepared = classify_writer_format(format, types);
                if (!(ct::expect(prepared.has_value()).note("specification: ", specification))) {
                    return;
                }
                for (auto value :
                     {0.0,
                      -0.0,
                      std::numeric_limits<double>::max(),
                      std::numeric_limits<double>::denorm_min(),
                      std::numeric_limits<double>::infinity()}) {
                    const auto output = std::vformat(
                        std::format("{{:{}}}", specification),
                        std::make_format_args(value)
                    );
                    ct::expect(output.size() >= prepared->minimum_size)
                        .note("specification: ", specification);
                    ct::expect(output.size() <= prepared->maximum_size)
                        .note("specification: ", specification);
                }
            }
            for (const auto specification : {".257f", "+.2f", ">12.2f", "a", "L"}) {
                ct::expect(
                    !(classify_writer_format(
                          FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}},
                          types
                    )
                          .has_value())
                );
            }
        }
    );

    ct::test(
        "Writer formatting: dynamic widths consume one additional operand per integer field",
        [] static noexcept {
            const auto types = std::array<std::optional<BuiltinType>, 5> {
                BuiltinType::I32,
                BuiltinType::U16,
                BuiltinType::Str,
                BuiltinType::I8,
                BuiltinType::I16
            };
            const auto prepared = classify_writer_format(
                FormatSpec {
                    .parts =
                        {format_text("prefix:"),
                         format_field(0uz, {format_text("0"), format_field(1uz), format_text("X")}),
                         format_field(2uz),
                         format_field(3uz, {format_field(4uz), format_text("b")})}
                },
                types
            );
            if (!ct::expect(prepared.has_value())) {
                return;
            }
            ct::expect(prepared->minimum_size == 7u);
            ct::expect(prepared->maximum_size == 7u);
            const auto* first = std::get_if<IntegerFormatField>(&prepared->fields[0]);
            if (!ct::expect(first != nullptr)) {
                return;
            }
            ct::expect(first->base == 16);
            ct::expect(first->uppercase);
            ct::expect(first->zero_pad);
            ct::expect(!(first->static_width.has_value()));
            ct::expect(writer_field_operand_count(prepared->fields[0]) == 2uz);
            ct::expect(writer_field_operand_count(prepared->fields[1]) == 1uz);
            const auto* last = std::get_if<IntegerFormatField>(&prepared->fields[2]);
            if (!ct::expect(last != nullptr)) {
                return;
            }
            ct::expect(last->base == 2);
            ct::expect(!(last->zero_pad));
            ct::expect(!(last->static_width.has_value()));
            ct::expect(writer_field_operand_count(prepared->fields[2]) == 2uz);
        }
    );

    ct::test(
        "Writer formatting: unsupported dynamic width shapes retain native formatting",
        [] static noexcept {
            const auto integer =
                std::array<std::optional<BuiltinType>, 2> {BuiltinType::I32, BuiltinType::I32};
            for (const auto prefix : {">", "+", "#", "00", "1", "."}) {
                ct::expect(
                    !(classify_writer_format(
                        FormatSpec {
                            .parts = {format_field(0uz, {format_text(prefix), format_field(1uz)})}
                        },
                        integer
                    ))
                )
                    .note("prefix: ", prefix);
            }
            for (const auto suffix : {"0", "1", "L", ".2f", "xx"}) {
                ct::expect(
                    !(classify_writer_format(
                        FormatSpec {
                            .parts = {format_field(0uz, {format_field(1uz), format_text(suffix)})}
                        },
                        integer
                    ))
                )
                    .note("suffix: ", suffix);
            }
            for (const auto type :
                 {BuiltinType::Bool,
                  BuiltinType::Char,
                  BuiltinType::F64,
                  BuiltinType::Str,
                  BuiltinType::U32,
                  BuiltinType::I64,
                  BuiltinType::U64}) {
                const auto types =
                    std::array<std::optional<BuiltinType>, 2> {BuiltinType::I32, type};
                ct::expect(!(classify_writer_format(
                               FormatSpec {.parts = {format_field(0uz, {format_field(1uz)})}},
                               types
                           )))
                    .note("static_cast<int>(type): ", static_cast<int>(type));
            }
        }
    );
});

} // namespace
