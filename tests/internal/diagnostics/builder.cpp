module carven:test.internal.diagnostics.builder;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.text;
import :test.harness.framework;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Diagnostic: builder preserves code, labels, notes, and severity"_test = [] static noexcept {
        const auto span = SourceSpan {
            .source_id = SourceID::from_index(0),
            .span = Span::from_bounds(2, 4),
        };
        const auto diagnostic = DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "example")
                                    .primary(span, "here")
                                    .related(span, "related")
                                    .note("additional context", span)
                                    .build();
        expect_equal(diagnostic.finding.severity, DiagnosticSeverity::Warning);
        expect_equal(
            diagnostic_code_info(diagnostic.finding.code).name,
            std::string_view("CV-FLOW-UNREACHABLE")
        );
        expect_equal(
            diagnostic_code_info(diagnostic.finding.code).default_severity,
            DiagnosticSeverity::Warning
        );
        if (!expect(diagnostic.attachment.primary.has_value())) {
            return;
        }
        expect_equal(diagnostic.attachment.primary->message, std::string_view("here"));
        if (!expect_equal(diagnostic.attachment.related.size(), 1u)) {
            return;
        }
        if (!expect_equal(diagnostic.attachment.notes.size(), 1u)) {
            return;
        }
        expect_equal(
            diagnostic.attachment.notes.front().message,
            std::string_view("additional context")
        );
        expect(diagnostic.attachment.notes.front().span.has_value());
    };

    "Diagnostic: builder supports findings without a source attachment"_test = [] static noexcept {
        const auto diagnostic =
            DiagnosticBuilder(DiagnosticCode::Catalog, "global failure").build();
        expect(!diagnostic.attachment.primary.has_value());
        expect_equal(
            diagnostic_code_info(diagnostic.finding.code).name,
            std::string_view("CV-CATALOG")
        );
    };
});

} // namespace
