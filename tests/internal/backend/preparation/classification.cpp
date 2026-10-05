module carven:test.internal.backend.preparation.classification;

import :backend.preparation.format;
import :semantic.format;
import :semantic.semir.type;
import :support.quote;
import :test.harness.framework;
import :test.internal.semantic.format.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Integer formatting: parsed fields retain literal bytes and exact type-derived sizes"_test =
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
            if (!expect(parsed.has_value())) {
                return;
            }
            expect(parsed->text == std::vector<std::string> {std::string("{我}\0", 6uz), "/", ""});
            expect(
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
            expect(parsed->minimum_size == 35u);
            expect(parsed->maximum_size == 35u);
        };

    "Integer formatting: size bounds include every value and the sign of a signed minimum"_test =
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
            each(
                scenarios,
                [](const Scenario& scenario) static noexcept -> std::string {
                    return quote_text(serialize_format(scenario.format));
                },
                [](const Scenario& scenario) static noexcept {
                    const auto types = std::array<std::optional<BuiltinType>, 1> {scenario.type};
                    const auto parsed = classify_writer_format(scenario.format, types);
                    if (!(expect(parsed.has_value()).note([&] noexcept {
                            return std::format(
                                "serialize_format(scenario.format): {}",
                                serialize_format(scenario.format)
                            );
                        }))) {
                        return;
                    }
                    expect(parsed->minimum_size == scenario.minimum_size).note([&] noexcept {
                        return std::format(
                            "serialize_format(scenario.format): {}",
                            serialize_format(scenario.format)
                        );
                    });
                    expect(parsed->maximum_size == scenario.maximum_size).note([&] noexcept {
                        return std::format(
                            "serialize_format(scenario.format): {}",
                            serialize_format(scenario.format)
                        );
                    });
                }
            );
        };

    "Integer formatting: unknown types and unsupported or malformed specifications remain general"_test =
        [] static noexcept {
            const auto integer = std::array<std::optional<BuiltinType>, 1> {BuiltinType::I32};
            for (const auto specification :
                 {"00", "+d", "#x", "c", "L", "2147483648", "999999999999999999999999"}) {
                expect(
                    !(classify_writer_format(
                          FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}},
                          integer
                    )
                          .has_value())
                )
                    .note("specification: ", specification);
            }
            expect(!(classify_writer_format(FormatSpec {.parts = {format_field(1uz)}}, integer)
                         .has_value()));
            expect(!(classify_writer_format(
                         FormatSpec {.parts = {format_field(0uz), format_field(0uz)}},
                         integer
            )
                         .has_value()));
            expect(!(classify_writer_format(
                         FormatSpec {.parts = {format_field(0uz, {format_field(1uz)})}},
                         integer
            )
                         .has_value()));
            for (const auto type :
                 {std::optional(BuiltinType::Void), std::optional<BuiltinType>()}) {
                const auto types = std::array {type};
                expect(!(classify_writer_format(FormatSpec {.parts = {format_field(0uz)}}, types)
                             .has_value()));
            }
        };

    "Writer formatting: mixed bounds exclude runtime text and include bool and Unicode scalars"_test =
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
            if (!expect(parsed.has_value())) {
                return;
            }
            expect(parsed->minimum_size == 9u);
            expect(parsed->maximum_size == 13u);
            expect(parsed->fields.size() == 5uz);
            for (const auto type :
                 {BuiltinType::Str, BuiltinType::String, BuiltinType::Bool, BuiltinType::Char}) {
                const auto operand = std::array<std::optional<BuiltinType>, 1> {type};
                expect(!(classify_writer_format(
                             FormatSpec {.parts = {format_field(0uz, {format_text(">8")})}},
                             operand
                )
                             .has_value()));
            }
        };

    "Floating formatting: direct policies bound output and delegate decorated or large formats"_test =
        [] static noexcept {
            const auto types = std::array<std::optional<BuiltinType>, 1uz> {BuiltinType::F64};
            for (const auto specification : {"", "f", ".2f", ".4e", ".17g", ".0", ".256f"}) {
                const auto format =
                    FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}};
                const auto prepared = classify_writer_format(format, types);
                if (!(expect(prepared.has_value()).note("specification: ", specification))) {
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
                    expect(output.size() >= prepared->minimum_size)
                        .note("specification: ", specification);
                    expect(output.size() <= prepared->maximum_size)
                        .note("specification: ", specification);
                }
            }
            for (const auto specification : {".257f", "+.2f", ">12.2f", "a", "L"}) {
                expect(
                    !(classify_writer_format(
                          FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}},
                          types
                    )
                          .has_value())
                );
            }
        };

    "Writer formatting: dynamic widths consume one additional operand per integer field"_test =
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
            if (!expect(prepared.has_value())) {
                return;
            }
            expect(prepared->minimum_size == 7u);
            expect(prepared->maximum_size == 7u);
            const auto* first = std::get_if<IntegerFormatField>(&prepared->fields[0]);
            if (!expect(first != nullptr)) {
                return;
            }
            expect(first->base == 16);
            expect(first->uppercase);
            expect(first->zero_pad);
            expect(!(first->static_width.has_value()));
            expect(writer_field_operand_count(prepared->fields[0]) == 2uz);
            expect(writer_field_operand_count(prepared->fields[1]) == 1uz);
            const auto* last = std::get_if<IntegerFormatField>(&prepared->fields[2]);
            if (!expect(last != nullptr)) {
                return;
            }
            expect(last->base == 2);
            expect(!(last->zero_pad));
            expect(!(last->static_width.has_value()));
            expect(writer_field_operand_count(prepared->fields[2]) == 2uz);
        };

    "Writer formatting: unsupported dynamic width shapes retain native formatting"_test =
        [] static noexcept {
            const auto integer =
                std::array<std::optional<BuiltinType>, 2> {BuiltinType::I32, BuiltinType::I32};
            for (const auto prefix : {">", "+", "#", "00", "1", "."}) {
                expect(
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
                expect(
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
                expect(!(classify_writer_format(
                           FormatSpec {.parts = {format_field(0uz, {format_field(1uz)})}},
                           types
                       )))
                    .note("static_cast<int>(type): ", static_cast<int>(type));
            }
        };
});

} // namespace
