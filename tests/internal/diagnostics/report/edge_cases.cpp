module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.report.edge_cases;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :diagnostics.report;
import :diagnostics.sink;
import :source.manager;
import :source.text;
import :test.internal.diagnostics.fixture;
import std;

TEST_CASE("Diagnostic report: tabs use fixed stops and UTF-8 uses scalar display columns") {
    SUBCASE("tab expansion") {
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

        CHECK_EQ(
            render_diagnostic(diagnostic, source),
            R"REPORT(error [CV-LEXICAL]: tab
 --> tab.cv:1:5
  |
1 | let value = 1;
  |     ^^^^^
)REPORT"
        );
    }

    SUBCASE("UTF-8 byte columns and display columns stay separate") {
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

        CHECK_EQ(
            render_diagnostic(diagnostic, source),
            "error [CV-LEXICAL]: UTF-8\n"
            " --> utf8.cv:1:7\n"
            "  |\n"
            "1 | // \xc3\xa9 value\n"
            "  |      ^^^^^\n"
        );
    }
}

TEST_CASE("Diagnostic report: empty source and EOF insertion points are safe") {
    SUBCASE("empty source") {
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
        CHECK_EQ(
            render_diagnostic(
                diagnostic,
                SourceView {
                    .source_id = SourceID::from_index(0),
                    .text = "",
                    .origin = "empty.cv",
                }
            ),
            "error [CV-LEXICAL]: empty\n"
            " --> empty.cv:1:1\n"
            "  |\n"
            "1 | \n"
            "  | ^\n"
        );
    }

    SUBCASE("EOF after visible text") {
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
        CHECK_EQ(
            render_diagnostic(
                diagnostic,
                SourceView {
                    .source_id = SourceID::from_index(0),
                    .text = "abc",
                    .origin = "eof.cv",
                }
            ),
            R"REPORT(error [CV-LEXICAL]: EOF
 --> eof.cv:1:4
  |
1 | abc
  |    ^
)REPORT"
        );
    }

    SUBCASE("EOF after a line terminator") {
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
        CHECK_EQ(
            render_diagnostic(
                diagnostic,
                SourceView {
                    .source_id = SourceID::from_index(0),
                    .text = "abc\n",
                    .origin = "eof.cv",
                }
            ),
            "error [CV-LEXICAL]: EOF\n"
            " --> eof.cv:2:1\n"
            "  |\n"
            "2 | \n"
            "  | ^\n"
        );
    }
}

TEST_CASE("Diagnostic report: malformed spans clamp deterministically") {
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

    CHECK_EQ(render_diagnostic(outside, source), render_diagnostic(eof, source));
    CHECK_EQ(
        render_diagnostic(partial, source),
        R"REPORT(error [CV-LEXICAL]: partial
 --> span.cv:1:2
  |
1 | abc
  |  ^^
)REPORT"
    );
}
