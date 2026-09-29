module carven:test.internal.source.location;

import :source.location;
import :source.text;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Source: spans are half-open byte ranges", [] static noexcept {
        static_assert(std::is_trivially_copyable_v<Span>);
        static_assert(!std::default_initializable<Span>);

        const auto span = Span::from_bounds(4, 9);
        ct::expect_equal(span.start(), 4u);
        ct::expect_equal(span.end(), 9u);
        ct::expect_equal(span.size(), 5u);
        ct::expect(!span.empty());
        ct::expect(Span::at(3).empty());
    });

    ct::test("Source: line lookup uses one-based lines and byte columns", [] static noexcept {
        const auto index = LineIndex("first\r\nsecond\n三");

        ct::expect_equal(index.location(0).line, 1u);
        ct::expect_equal(index.location(0).column, 1u);
        ct::expect_equal(index.location(7).line, 2u);
        ct::expect_equal(index.location(7).column, 1u);
        ct::expect_equal(index.location(13).line, 2u);
        ct::expect_equal(index.location(13).column, 7u);
        ct::expect_equal(index.location(14).line, 3u);
        ct::expect_equal(index.location(14).column, 1u);
        ct::expect_equal(index.location(17).line, 3u);
        ct::expect_equal(index.location(17).column, 4u);
        ct::expect_equal(index.location(99).line, 3u);
        ct::expect_equal(index.location(99).column, 4u);
    });

    ct::test(
        "Source: indexed line ranges retain terminators and the final line",
        [] static noexcept {
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
            ct::each(cases, &Case::text, [](const Case& value) static noexcept {
                const auto index = LineIndex(value.text);
                auto offset = 0u;
                for (auto line = 1u; line <= value.lines.size(); ++line) {
                    const auto span = index.line_span(line);
                    ct::expect_equal(span.start(), offset).note("value.text: ", value.text);
                    ct::expect_equal(slice(value.text, span), value.lines[line - 1])
                        .note("value.text: ", value.text);
                    ct::expect_equal(index.location(span.start()).line, line)
                        .note("value.text: ", value.text);
                    offset = span.end();
                }
                ct::expect_equal(offset, value.text.size()).note("value.text: ", value.text);
            });
        }
    );
});

} // namespace
