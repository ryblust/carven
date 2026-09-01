module carven:semantic.analysis.availability.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.analyzer;
import :semantic.analysis.availability;
import :semantic.analysis.availability.build;
import :semantic.analysis.availability.place;
import :semantic.analysis.availability.solve;
import :semantic.analysis.session.read;
import :source.text;
import std;

namespace {

auto publish_findings(
    SemanticDraftView hir,
    DiagnosticSink& diagnostics,
    const BodyAvailabilityFindings& findings
) noexcept -> void {
    for (const auto& finding : findings.unavailable_uses) {
        diagnostics.emit(DiagnosticBuilder(
                             DiagnosticCode::AccessUnavailable,
                             "binding is unavailable after Take"
        )
                             .primary(
                                 diagnostic_span(hir, hir.expression(finding.expression).origin),
                                 "unavailable use"
                             )
                             .related(
                                 diagnostic_span(hir, finding.site.origin),
                                 "binding became unavailable here"
                             )
                             .build());
    }
    for (const auto& finding : findings.operation_conflicts) {
        auto diagnostic =
            DiagnosticBuilder(
                DiagnosticCode::AccessOperationConflict,
                "one operation cannot both access and Take the same binding"
            )
                .primary(diagnostic_span(hir, hir.expression(finding.primary).origin));
        if (finding.related.has_value()) {
            diagnostic.related(
                diagnostic_span(hir, hir.expression(*finding.related).origin),
                "the assignment target accesses the same binding"
            );
        }
        diagnostics.emit(std::move(diagnostic).build());
    }
    for (const auto& finding : findings.invalid_takes) {
        const auto* message = finding.kind == InvalidTakeKind::Partial
            ? "partial Take from a member or element is not supported"
            : "Take requires a whole runtime owner binding or a temporary";
        diagnostics.emit(DiagnosticBuilder(DiagnosticCode::AccessTakeOperand, message)
                             .primary(diagnostic_span(hir, finding.origin))
                             .build());
    }
    for (const auto& finding : findings.capture_conflicts) {
        diagnostics.emit(
            DiagnosticBuilder(
                DiagnosticCode::AccessCaptureConflict,
                "binding cannot be both a Write capture source and a Take source in one callable"
            )
                .primary(diagnostic_span(hir, finding.take_site.origin), "Take source")
                .related(diagnostic_span(hir, finding.capture_origin), "Write capture source")
                .build()
        );
    }
}
auto diagnose_body(
    SemanticDraftView hir,
    DiagnosticSink& diagnostics,
    const AvailabilityPlaceCatalog& catalog,
    BodyID body
) noexcept -> void {
    const auto graph = BodyAvailabilityGraphBuilder(hir, catalog, body).build();
    publish_findings(hir, diagnostics, solve_body_availability(hir, catalog, body, graph));
}

} // namespace

auto diagnose_availability(SemanticDraftView builder, DiagnosticSink& diagnostics) noexcept
    -> void {
    const auto catalog = AvailabilityPlaceCatalog(builder);
    for (auto index = 0uz; index < builder.callables().size(); ++index) {
        const auto callable = CallableID::from_index(static_cast<std::uint32_t>(index));
        diagnose_body(builder, diagnostics, catalog, builder.callable(callable).body);
    }
    for (auto index = 0uz; index < builder.tests().size(); ++index) {
        const auto test = TestID::from_index(static_cast<std::uint32_t>(index));
        diagnose_body(builder, diagnostics, catalog, builder.test(test).body);
    }
}
