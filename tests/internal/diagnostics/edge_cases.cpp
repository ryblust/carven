module carven:test.internal.diagnostics.edge_cases;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :diagnostics.report;
import :diagnostics.sink;
import :source.manager;
import :source.text;
import :test.harness.framework;
import :test.internal.diagnostics.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Diagnostic report: long lines keep the marked location visible", [] static noexcept {
        const auto positions = std::array {0u, 200u, 399u};
        ct::each(
            positions,
            [](auto position) static noexcept { return std::to_string(position); },
            [](auto position) static noexcept {
                auto text = std::string(400, 'a');
                text[position] = '!';
                const auto diagnostic = make_diagnostic(
                    "long line",
                    {
                        .span =
                            {.source_id = SourceID::from_index(0),
                             .span = Span::from_bounds(position, position + 1)},
                        .message = {},
                    }
                );
                const auto output = render_diagnostic(
                    diagnostic,
                    {
                        .source_id = SourceID::from_index(0),
                        .text = text,
                        .origin = "long.cv",
                    }
                );
                const auto source_begin = output.find("1 | ");
                if (!ct::expect_not_equal(source_begin, std::string::npos)) {
                    return;
                }
                const auto source_end = output.find('\n', source_begin);
                const auto marker_end = output.find('\n', source_end + 1);
                const auto source_line =
                    std::string_view(output).substr(source_begin, source_end - source_begin);
                const auto marker_line =
                    std::string_view(output).substr(source_end + 1, marker_end - source_end - 1);
                ct::expect(source_line.size() < text.size());
                ct::expect(source_line.contains("..."));
                ct::expect_not_equal(source_line.find('!'), std::string_view::npos);
                ct::expect_equal(source_line.find('!'), marker_line.find('^'));
                ct::expect(output.contains(std::format("long.cv:1:{}", position + 1)));
            }
        );
    });

    ct::test("Diagnostic report: tabs expand at fixed stops", [] static noexcept {
        static constexpr auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = "let\tvalue = 1;",
            .origin = "tab.cv",
        };
        const auto diagnostic = make_diagnostic(
            "tab",
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(4, 9),
                    },
                .message = {},
            }
        );

        ct::expect_equal(
            render_diagnostic(diagnostic, source),
            std::string_view(R"REPORT(error [CV-LEXICAL]: tab
 --> tab.cv:1:5
  |
1 | let value = 1;
  |     ^^^^^
)REPORT")
        );
    });

    ct::test(
        "Diagnostic report: UTF-8 byte columns and display columns stay separate",
        [] static noexcept {
            static constexpr auto source = SourceView {
                .source_id = SourceID::from_index(0),
                .text = "// \xc3\xa9 value",
                .origin = "utf8.cv",
            };
            const auto diagnostic = make_diagnostic(
                "UTF-8",
                {
                    .span =
                        {
                            .source_id = SourceID::from_index(0),
                            .span = Span::from_bounds(6, 11),
                        },
                    .message = {},
                }
            );

            ct::expect_equal(
                render_diagnostic(diagnostic, source),
                std::string_view(
                    "error [CV-LEXICAL]: UTF-8\n"
                    " --> utf8.cv:1:7\n"
                    "  |\n"
                    "1 | // \xc3\xa9 value\n"
                    "  |      ^^^^^\n"
                )
            );
        }
    );

    ct::test("Diagnostic report: empty sources render an insertion point", [] static noexcept {
        const auto diagnostic = make_diagnostic(
            "empty",
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::at(0),
                    },
                .message = {},
            }
        );
        ct::expect(
            (render_diagnostic(
                 diagnostic,
                 SourceView {
                     .source_id = SourceID::from_index(0),
                     .text = "",
                     .origin = "empty.cv",
                 }
             )
             == std::string_view(
                 "error [CV-LEXICAL]: empty\n"
                 " --> empty.cv:1:1\n"
                 "  |\n"
                 "1 | \n"
                 "  | ^\n"
             ))
        );
    });

    ct::test(
        "Diagnostic report: EOF after visible text renders at the trailing column",
        [] static noexcept {
            const auto diagnostic = make_diagnostic(
                "EOF",
                {
                    .span =
                        {
                            .source_id = SourceID::from_index(0),
                            .span = Span::from_bounds(3, 3),
                        },
                    .message = {},
                }
            );
            ct::expect(
                (render_diagnostic(
                     diagnostic,
                     SourceView {
                         .source_id = SourceID::from_index(0),
                         .text = "abc",
                         .origin = "eof.cv",
                     }
                 )
                 == std::string_view(R"REPORT(error [CV-LEXICAL]: EOF
 --> eof.cv:1:4
  |
1 | abc
  |    ^
)REPORT"))
            );
        }
    );

    ct::test(
        "Diagnostic report: EOF after a line terminator renders on the next line",
        [] static noexcept {
            const auto diagnostic = make_diagnostic(
                "EOF",
                {
                    .span =
                        {
                            .source_id = SourceID::from_index(0),
                            .span = Span::from_bounds(4, 4),
                        },
                    .message = {},
                }
            );
            ct::expect(
                (render_diagnostic(
                     diagnostic,
                     SourceView {
                         .source_id = SourceID::from_index(0),
                         .text = "abc\n",
                         .origin = "eof.cv",
                     }
                 )
                 == std::string_view(
                     "error [CV-LEXICAL]: EOF\n"
                     " --> eof.cv:2:1\n"
                     "  |\n"
                     "2 | \n"
                     "  | ^\n"
                 ))
            );
        }
    );

    ct::test("Diagnostic report: malformed spans clamp deterministically", [] static noexcept {
        const auto eof = make_diagnostic(
            "invalid span",
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(3, 3),
                    },
                .message = {},
            }
        );
        const auto outside = make_diagnostic(
            "invalid span",
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(99, 120),
                    },
                .message = {},
            }
        );
        const auto partial = make_diagnostic(
            "partial",
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(1, 99),
                    },
                .message = {},
            }
        );
        static constexpr auto source = SourceView {
            .source_id = SourceID::from_index(0),
            .text = "abc",
            .origin = "span.cv",
        };

        ct::expect_equal(render_diagnostic(outside, source), render_diagnostic(eof, source));
        ct::expect_equal(
            render_diagnostic(partial, source),
            std::string_view(R"REPORT(error [CV-LEXICAL]: partial
 --> span.cv:1:2
  |
1 | abc
  |  ^^
)REPORT")
        );
    });

    ct::test(
        "Diagnostic report: CRLF boundaries retain byte locations without visible terminators",
        [] static noexcept {
            const auto source = SourceView {
                .source_id = SourceID::from_index(0),
                .text = "a\r\nb\r\n",
                .origin = "crlf.cv",
            };
            const auto diagnostic = make_diagnostic(
                "range",
                {
                    .span = {.source_id = source.source_id, .span = Span::from_bounds(1, 5)},
                    .message = "end",
                }
            );
            ct::expect_equal(
                render_diagnostic(diagnostic, source),
                std::string_view(
                    "error [CV-LEXICAL]: range\n --> crlf.cv:1:2\n  |\n1 | a\n  |  ^\n2 | b\n  | ^ end\n"
                )
            );
        }
    );
});

} // namespace
