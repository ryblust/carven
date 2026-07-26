module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.support.path;

import :support.path;
import std;

TEST_CASE("Support path: UTF-8 and generic separators round trip") {
    const auto encoded = std::string("目录/模块.cv");
    const auto path = path_from_utf8(encoded);

    CHECK_EQ(path_to_generic_utf8(path), encoded);
    CHECK_EQ(
        path_to_generic_utf8(path_from_utf8("alpha\\beta.cv")),
        std::filesystem::path::preferred_separator == '\\' ? "alpha/beta.cv" : "alpha\\beta.cv"
    );
}
