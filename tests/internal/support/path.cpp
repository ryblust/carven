module carven:test.internal.support.path;

import :support.path;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Support path: UTF-8 and generic separators round trip", [] static noexcept {
        const auto encoded = std::string("目录/模块.cv");
        const auto path = path_from_utf8(encoded);

        ct::expect_equal(path_to_generic_utf8(path), encoded);
        const auto expected = std::filesystem::path::preferred_separator == '\\'
            ? std::string_view("alpha/beta.cv")
            : std::string_view("alpha\\beta.cv");
        ct::expect_equal(path_to_generic_utf8(path_from_utf8("alpha\\beta.cv")), expected);
    });
});

} // namespace
