module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.diagnostics.builder;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.text;
import std;

TEST_CASE("Diagnostic: builder preserves code, labels, notes, and severity") {
    const auto span = SourceSpan {
        .source_id = SourceID::from_index(0),
        .span = Span::from_bounds(2, 4),
    };
    const auto diagnostic = DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "example")
                                .primary(span, "here")
                                .related(span, "related")
                                .note("additional context", span)
                                .build();
    CHECK_EQ(diagnostic.finding.severity, DiagnosticSeverity::Warning);
    CHECK_EQ(diagnostic.finding.code, "CV-FLOW-UNREACHABLE");
    CHECK_EQ(
        diagnostic_code_info(diagnostic.finding.code).default_severity,
        DiagnosticSeverity::Warning
    );
    REQUIRE(diagnostic.attachment.primary.has_value());
    CHECK_EQ(diagnostic.attachment.primary->message, "here");
    REQUIRE_EQ(diagnostic.attachment.related.size(), 1u);
    REQUIRE_EQ(diagnostic.attachment.notes.size(), 1u);
    CHECK_EQ(diagnostic.attachment.notes.front().message, "additional context");
    CHECK(diagnostic.attachment.notes.front().span.has_value());
}

TEST_CASE("Diagnostic: builder supports findings without a source attachment") {
    const auto diagnostic = DiagnosticBuilder(DiagnosticCode::Catalog, "global failure").build();
    CHECK(!diagnostic.attachment.primary.has_value());
    CHECK_EQ(diagnostic.finding.code, "CV-CATALOG");
}
