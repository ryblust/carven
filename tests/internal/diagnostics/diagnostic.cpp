module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.diagnostic;

import :diagnostics.diagnostic;
import :source.text;
import :test.internal.diagnostics.fixture;
import std;

TEST_CASE("Diagnostic: value contract has one primary and ordered secondary labels") {
    const auto diagnostic = make_diagnostic(
        "duplicate definition",
        {
            .span =
                {
                    .source_id = SourceID::from_index(0),
                    .span = Span::from_bounds(20, 24),
                },
            .message = "duplicate",
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
            {
                .span =
                    {
                        .source_id = SourceID::from_index(0),
                        .span = Span::from_bounds(10, 14),
                    },
                .message = "related definition",
            },
        }
    );

    CHECK_EQ(diagnostic.finding.message, "duplicate definition");
    REQUIRE(diagnostic.attachment.primary.has_value());
    CHECK_EQ(diagnostic.attachment.primary->span.span.start(), 20u);
    CHECK_EQ(diagnostic.attachment.primary->span.span.end(), 24u);
    CHECK_EQ(diagnostic.attachment.primary->message, "duplicate");
    REQUIRE_EQ(diagnostic.attachment.related.size(), 2u);
    CHECK_EQ(diagnostic.attachment.related[0].message, "first definition");
    CHECK_EQ(diagnostic.attachment.related[1].message, "related definition");
}
