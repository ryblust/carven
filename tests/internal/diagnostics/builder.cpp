module carven:test.internal.diagnostics.builder;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :source.text;
import :test.harness.framework;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test("Diagnostic: builder preserves code, labels, notes, and severity", [] static noexcept {
        const auto span = SourceSpan {
            .source_id = SourceID::from_index(0),
            .span = Span::from_bounds(2, 4),
        };
        const auto diagnostic = DiagnosticBuilder(DiagnosticCode::FlowUnreachable, "example")
                                    .primary(span, "here")
                                    .related(span, "related")
                                    .note("additional context", span)
                                    .build();
        ct::expect_equal(diagnostic.finding.severity, DiagnosticSeverity::Warning);
        ct::expect_equal(
            diagnostic_code_info(diagnostic.finding.code).name,
            std::string_view("CV-FLOW-UNREACHABLE")
        );
        ct::expect_equal(
            diagnostic_code_info(diagnostic.finding.code).default_severity,
            DiagnosticSeverity::Warning
        );
        if (!ct::expect(diagnostic.attachment.primary.has_value())) {
            return;
        }
        ct::expect_equal(diagnostic.attachment.primary->message, std::string_view("here"));
        if (!ct::expect_equal(diagnostic.attachment.related.size(), 1u)) {
            return;
        }
        if (!ct::expect_equal(diagnostic.attachment.notes.size(), 1u)) {
            return;
        }
        ct::expect_equal(
            diagnostic.attachment.notes.front().message,
            std::string_view("additional context")
        );
        ct::expect(diagnostic.attachment.notes.front().span.has_value());
    });

    ct::test(
        "Diagnostic: builder supports findings without a source attachment",
        [] static noexcept {
            const auto diagnostic =
                DiagnosticBuilder(DiagnosticCode::Catalog, "global failure").build();
            ct::expect(!diagnostic.attachment.primary.has_value());
            ct::expect_equal(
                diagnostic_code_info(diagnostic.finding.code).name,
                std::string_view("CV-CATALOG")
            );
        }
    );
});

} // namespace
