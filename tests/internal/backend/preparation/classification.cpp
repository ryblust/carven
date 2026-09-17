module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.preparation.classification;

import :backend.preparation.format;
import :semantic.format;
import :semantic.semir.type;
import :test.internal.semantic.format.fixture;
import std;

TEST_CASE("Integer formatting: parsed fields retain literal bytes and exact type-derived sizes") {
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
    REQUIRE(parsed.has_value());
    CHECK(parsed->text == std::vector<std::string> {std::string("{我}\0", 6uz), "/", ""});
    CHECK(
        parsed->fields
        == std::vector<WriterFormatField> {
            IntegerFormatField {.base = 16, .uppercase = true, .zero_pad = true, .width = 8u},
            IntegerFormatField {.base = 10, .uppercase = false, .zero_pad = true, .width = 20u},
        }
    );
    CHECK(parsed->minimum_size == 35u);
    CHECK(parsed->maximum_size == 35u);
}

TEST_CASE("Integer formatting: size bounds include every value and the sign of a signed minimum") {
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
    for (const auto& scenario : scenarios) {
        CAPTURE(serialize_format(scenario.format));
        const auto types = std::array<std::optional<BuiltinType>, 1> {scenario.type};
        const auto parsed = classify_writer_format(scenario.format, types);
        REQUIRE(parsed.has_value());
        CHECK(parsed->minimum_size == scenario.minimum_size);
        CHECK(parsed->maximum_size == scenario.maximum_size);
    }
}

TEST_CASE(
    "Integer formatting: unknown types and unsupported or malformed specifications remain general"
) {
    const auto integer = std::array<std::optional<BuiltinType>, 1> {BuiltinType::I32};
    for (const auto specification :
         {"00", "+d", "#x", "c", "L", "2147483648", "999999999999999999999999"}) {
        CAPTURE(specification);
        CHECK_FALSE(classify_writer_format(
                        FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}},
                        integer
        )
                        .has_value());
    }
    CHECK_FALSE(
        classify_writer_format(FormatSpec {.parts = {format_field(1uz)}}, integer).has_value()
    );
    CHECK_FALSE(classify_writer_format(
                    FormatSpec {.parts = {format_field(0uz), format_field(0uz)}},
                    integer
    )
                    .has_value());
    CHECK_FALSE(classify_writer_format(
                    FormatSpec {.parts = {format_field(0uz, {format_field(1uz)})}},
                    integer
    )
                    .has_value());
    for (const auto type : {std::optional(BuiltinType::Void), std::optional<BuiltinType>()}) {
        const auto types = std::array {type};
        CHECK_FALSE(
            classify_writer_format(FormatSpec {.parts = {format_field(0uz)}}, types).has_value()
        );
    }
}

TEST_CASE(
    "Writer formatting: mixed bounds exclude runtime text and include bool and Unicode scalars"
) {
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
    REQUIRE(parsed.has_value());
    CHECK(parsed->minimum_size == 9u);
    CHECK(parsed->maximum_size == 13u);
    CHECK(parsed->fields.size() == 5uz);
    for (const auto type :
         {BuiltinType::Str, BuiltinType::String, BuiltinType::Bool, BuiltinType::Char}) {
        const auto operand = std::array<std::optional<BuiltinType>, 1> {type};
        CHECK_FALSE(classify_writer_format(
                        FormatSpec {.parts = {format_field(0uz, {format_text(">8")})}},
                        operand
        )
                        .has_value());
    }
}

TEST_CASE(
    "Floating formatting: direct policies bound output and delegate decorated or large formats"
) {
    const auto types = std::array<std::optional<BuiltinType>, 1uz> {BuiltinType::F64};
    for (const auto specification : {"", "f", ".2f", ".4e", ".17g", ".0", ".256f"}) {
        CAPTURE(specification);
        const auto format = FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}};
        const auto prepared = classify_writer_format(format, types);
        REQUIRE(prepared.has_value());
        for (auto value :
             {0.0,
              -0.0,
              std::numeric_limits<double>::max(),
              std::numeric_limits<double>::denorm_min(),
              std::numeric_limits<double>::infinity()}) {
            const auto output =
                std::vformat(std::format("{{:{}}}", specification), std::make_format_args(value));
            CHECK(output.size() >= prepared->minimum_size);
            CHECK(output.size() <= prepared->maximum_size);
        }
    }
    for (const auto specification : {".257f", "+.2f", ">12.2f", "a", "L"}) {
        CHECK_FALSE(classify_writer_format(
                        FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}},
                        types
        )
                        .has_value());
    }
}
