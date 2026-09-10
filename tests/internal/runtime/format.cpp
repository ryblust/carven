module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>
#include <carven/runtime/format.hpp>

module carven:test.internal.runtime.format;

import std;

TEST_CASE("Runtime: formatting owns validated UTF-8 including NUL") {
    const auto text = carven::runtime::String::from_str("é我😀");
    CHECK(carven::runtime::format("{} {}", text, U'😀').as_str() == "é我😀 😀");
    CHECK(
        carven::runtime::format(std::string_view("a\0{0}", 5), 7).as_str()
        == std::string_view(
            "a\0"
            "7",
            3
        )
    );
    CHECK(carven::runtime::format("{{}} {:04x} {:.2f}", 42, 1.25).as_str() == "{} 002a 1.25");
    CHECK(carven::runtime::format("{:>{}}", U'我', 4).as_str() == "  我");
    CHECK(carven::runtime::format("{0:{1}.{2}f}", 1.25, 7, 1).as_str() == "    1.2");
}
