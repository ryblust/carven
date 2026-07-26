module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.emission.render;

import :backend.emission.render.string;

TEST_CASE("Emission: C++ string quoting owns escape syntax") {
    CHECK_EQ(cpp_string_token("a\\b\n\"c\t"), "\"a\\\\b\\012\\\"c\\011\"");
    CHECK_EQ(cpp_string_token(std::string_view("\0018\377", 3)), "\"\\0018\\377\"");
}
