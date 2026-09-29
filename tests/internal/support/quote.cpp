module carven:test.internal.support.quote;

import :support.quote;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Support quote: text remains readable and bytes remain explicit", [] static noexcept {
        struct Case final {
            std::string_view name;
            std::string_view text;
            std::string_view expected;
        };
        const auto cases = std::to_array<Case>({
            {.name = "empty", .text = "", .expected = R"("")"},
            {.name = "UTF-8", .text = "你好 café", .expected = R"("你好 café")"},
            {.name = "quotes", .text = "\"\\", .expected = R"("\"\\")"},
            {.name = "control bytes",
             .text = std::string_view("\n\r\t\0\x1b\x7f", 6),
             .expected = R"("\n\r\t\x00\x1b\x7f")"},
            {.name = "invalid UTF-8",
             .text = "\xff\xc0\x80中\xe2\x82",
             .expected = R"("\xff\xc0\x80中\xe2\x82")"},
        });
        ct::each(cases, &Case::name, [](const Case& entry) static noexcept {
            ct::expect_equal(quote_text(entry.text), entry.expected);
        });
    });
});

} // namespace
