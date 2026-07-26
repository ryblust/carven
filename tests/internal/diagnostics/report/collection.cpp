module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.report.collection;

import :diagnostics.report;
import :source.text;
import :test.internal.diagnostics.fixture;
import std;

TEST_CASE("Diagnostic report: collections preserve order and use one separator") {
    static constexpr auto source = SourceView {
        .source_id = SourceID::from_index(0),
        .text = "@",
        .origin = "bad.cv",
    };
    const auto diagnostics = std::array {
        make_diagnostic(
            "first",
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(0, 1),
                    },
                .message = {},
            }
        ),
        make_diagnostic(
            "second",
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(1, 1),
                    },
                .message = {},
            }
        ),
    };

    CHECK(render_diagnostics({}, source).empty());
    const auto rendered = render_diagnostics(diagnostics, source);
    const auto first = rendered.find("first");
    const auto second = rendered.find("second");
    REQUIRE(first != std::string::npos);
    REQUIRE(second != std::string::npos);
    CHECK(first < second);
    const auto separator = rendered.find("\n\n", first);
    REQUIRE(separator != std::string::npos);
    CHECK_EQ(separator, rendered.rfind("\n\n"));
}
