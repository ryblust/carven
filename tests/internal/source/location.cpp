module carven:test.internal.source.location;

import :source.location;
import :source.text;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Source: line lookup uses one-based lines and byte columns"_test = [] static noexcept {
        const auto index = LineIndex("first\r\nsecond\n三");

        expect_equal(index.location(0).line, 1u);
        expect_equal(index.location(0).column, 1u);
        expect_equal(index.location(7).line, 2u);
        expect_equal(index.location(7).column, 1u);
        expect_equal(index.location(13).line, 2u);
        expect_equal(index.location(13).column, 7u);
        expect_equal(index.location(14).line, 3u);
        expect_equal(index.location(14).column, 1u);
        expect_equal(index.location(17).line, 3u);
        expect_equal(index.location(17).column, 4u);
        expect_equal(index.location(99).line, 3u);
        expect_equal(index.location(99).column, 4u);
    };

    "Source: indexed line ranges retain terminators and the final line"_test = [] static noexcept {
        struct Case final {
            std::string_view text;
            std::vector<std::string_view> lines;
        };

        const auto cases = std::array {
            Case {.text = "", .lines = {""}},
            Case {.text = "last", .lines = {"last"}},
            Case {.text = "a\n", .lines = {"a\n", ""}},
            Case {.text = "a\r\n\r\nlast\r", .lines = {"a\r\n", "\r\n", "last\r"}},
        };
        each(cases, &Case::text, [](const Case& value) static noexcept {
            const auto index = LineIndex(value.text);
            auto offset = 0u;
            for (auto line = 1u; line <= value.lines.size(); ++line) {
                const auto span = index.line_span(line);
                expect_equal(span.start(), offset).note("value.text: ", value.text);
                expect_equal(slice(value.text, span), value.lines[line - 1])
                    .note("value.text: ", value.text);
                expect_equal(index.location(span.start()).line, line)
                    .note("value.text: ", value.text);
                offset = span.end();
            }
            expect_equal(offset, value.text.size()).note("value.text: ", value.text);
        });
    };
});

} // namespace
