module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.report.layout;

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

TEST_CASE("Diagnostic report: single-line primary matches the accepted main snapshot") {
    static constexpr auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "fn main(value: i32) {\n",
        .origin = "app.cv",
    };
    const auto diagnostic = make_diagnostic(
        "`main` accepts no parameters or one untyped parameter",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(8, 18),
                },
            .message = "typed parameters are not allowed",
        }
    );

    CHECK_EQ(
        render_diagnostic(diagnostic, source),
        R"REPORT(error [CV-LEXICAL]: `main` accepts no parameters or one untyped parameter
 --> app.cv:1:9
  |
1 | fn main(value: i32) {
  |         ^^^^^^^^^^ typed parameters are not allowed
)REPORT"
    );
}

TEST_CASE("Diagnostic report: zero-width syntax spans render one insertion caret") {
    static constexpr auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "fn main( {}",
        .origin = "app.cv",
    };
    const auto diagnostic = make_diagnostic(
        "expected `)` after function parameters",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(8, 8),
                },
            .message = "expected `)` here",
        }
    );

    CHECK_EQ(
        render_diagnostic(diagnostic, source),
        R"REPORT(error [CV-LEXICAL]: expected `)` after function parameters
 --> app.cv:1:9
  |
1 | fn main( {}
  |         ^ expected `)` here
)REPORT"
    );
}

TEST_CASE("Diagnostic report: terminal control bytes are escaped") {
    const auto text = std::string("a\0b", 3);
    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "control.cv",
    };
    const auto diagnostic = make_diagnostic(
        "control byte",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(1, 2),
                },
            .message = "escaped",
        }
    );

    CHECK_EQ(
        render_diagnostic(diagnostic, source),
        R"REPORT(error [CV-LEXICAL]: control byte
 --> control.cv:1:2
  |
1 | a\x00b
  |  ^^^^ escaped
)REPORT"
    );
}

TEST_CASE("Diagnostic report: long multi-line spans show bounded endpoints and ellipsis") {
    static constexpr auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "zero\n"
                "first line\n"
                "middle one\n"
                "middle two\n"
                "last line\n"
                "end\n",
        .origin = "app.cv",
    };
    const auto diagnostic = make_diagnostic(
        "multi-line example",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(11, 42),
                },
            .message = "bounded region",
        }
    );

    CHECK_EQ(
        render_diagnostic(diagnostic, source),
        R"REPORT(error [CV-LEXICAL]: multi-line example
 --> app.cv:2:7
  |
2 | first line
  |       ^^^^
  | ...
5 | last line
  | ^^^^ bounded region
)REPORT"
    );
}

TEST_CASE("Diagnostic report: adjacent multi-line spans do not add an ellipsis") {
    static constexpr auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "first\nsecond\n",
        .origin = "app.cv",
    };
    const auto diagnostic = make_diagnostic(
        "two lines",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(2, 9),
                },
            .message = "end",
        }
    );
    const auto output = render_diagnostic(diagnostic, source);

    CHECK(!output.contains("..."));
    CHECK_EQ(output, R"REPORT(error [CV-LEXICAL]: two lines
 --> app.cv:1:3
  |
1 | first
  |   ^^^
2 | second
  | ^^^ end
)REPORT");
}

TEST_CASE(
    "Diagnostic report: labels render in source order without changing "
    "the primary location"
) {
    static constexpr auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "fn main() {}\n"
                "\n"
                "fn helper() {}\n"
                "\n"
                "fn main(args) {}\n",
        .origin = "app.cv",
    };
    const auto diagnostic = make_diagnostic(
        "`main` is defined more than once",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(33, 37),
                },
            .message = "duplicate definition",
        },
        {
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(3, 7),
                    },
                .message = "first definition",
            },
        }
    );

    CHECK_EQ(
        render_diagnostic(diagnostic, source),
        R"REPORT(error [CV-LEXICAL]: `main` is defined more than once
 --> app.cv:5:4
  |
1 | fn main() {}
  |    ---- first definition
  |
5 | fn main(args) {}
  |    ^^^^ duplicate definition
)REPORT"
    );
}

TEST_CASE("Diagnostic report: one diagnostic can render labels from multiple sources") {
    auto sources = SourceManager();
    const auto first = *sources.append_virtual("first.cv", "export fn value() {}\n");
    const auto second = *sources.append_virtual("second.cv", "export fn value() {}\n");
    const auto diagnostic = make_diagnostic(
        "duplicate exported declaration",
        {
            .span = locate(second, Span::from_bounds(10, 15)),
            .message = "duplicate declaration",
        },
        {
            {
                .span = locate(first, Span::from_bounds(10, 15)),
                .message = "first declaration",
            },
        }
    );

    CHECK_EQ(
        ::render_diagnostic(diagnostic, sources),
        R"REPORT(error [CV-LEXICAL]: duplicate exported declaration
 --> second.cv:1:11
  |
1 | export fn value() {}
  |           ^^^^^ duplicate declaration
 ::: first.cv:1:11
  |
1 | export fn value() {}
  |           ----- first declaration
)REPORT"
    );
}

TEST_CASE("Diagnostic report: same-line labels retain deterministic tie order") {
    static constexpr auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "abcdef",
        .origin = "app.cv",
    };
    const auto diagnostic = make_diagnostic(
        "overlap",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(1, 3),
                },
            .message = "primary",
        },
        {
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(1, 3),
                    },
                .message = "secondary",
            },
        }
    );

    CHECK_EQ(
        render_diagnostic(diagnostic, source),
        R"REPORT(error [CV-LEXICAL]: overlap
 --> app.cv:1:2
  |
1 | abcdef
  |  ^^ primary
  |
1 | abcdef
  |  -- secondary
)REPORT"
    );
}

TEST_CASE("Diagnostic report: gutters grow to the largest displayed line number") {
    static constexpr auto text = std::string_view("1\n2\n3\n4\n5\n6\n7\n8\n9\ntarget");
    const auto start = static_cast<std::uint32_t>(text.find("target"));
    const auto diagnostic = make_diagnostic(
        "line ten",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(start, start + 6),
                },
            .message = {},
        }
    );

    const auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = text,
        .origin = "ten.cv",
    };
    CHECK_EQ(
        render_diagnostic(diagnostic, source),
        R"REPORT(error [CV-LEXICAL]: line ten
 --> ten.cv:10:1
   |
10 | target
   | ^^^^^^
)REPORT"
    );
}
