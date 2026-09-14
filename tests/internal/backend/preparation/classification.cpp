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
    const auto parsed = classify_integer_format(
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
        == std::vector<IntegerFormatField> {
            {.base = 16, .uppercase = true, .zero_pad = true, .width = 8u},
            {.base = 10, .uppercase = false, .zero_pad = true, .width = 20u},
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
        const auto parsed = classify_integer_format(scenario.format, types);
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
        CHECK_FALSE(classify_integer_format(
                        FormatSpec {.parts = {format_field(0uz, {format_text(specification)})}},
                        integer
        )
                        .has_value());
    }
    CHECK_FALSE(
        classify_integer_format(FormatSpec {.parts = {format_field(1uz)}}, integer).has_value()
    );
    CHECK_FALSE(classify_integer_format(
                    FormatSpec {.parts = {format_field(0uz), format_field(0uz)}},
                    integer
    )
                    .has_value());
    CHECK_FALSE(classify_integer_format(
                    FormatSpec {.parts = {format_field(0uz, {format_field(1uz)})}},
                    integer
    )
                    .has_value());
    for (const auto type :
         {std::optional(BuiltinType::Bool),
          std::optional(BuiltinType::Str),
          std::optional(BuiltinType::F64),
          std::optional<BuiltinType>()}) {
        const auto types = std::array {type};
        CHECK_FALSE(
            classify_integer_format(FormatSpec {.parts = {format_field(0uz)}}, types).has_value()
        );
    }
}
