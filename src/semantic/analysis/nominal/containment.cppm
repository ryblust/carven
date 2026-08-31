module carven:semantic.analysis.nominal.containment;

import :diagnostics.sink;
import :semantic.analysis.session;
import :semantic.analysis.session.read;

auto analyze_nominal_containment(SemanticDraft& construction, DiagnosticSink& diagnostics) noexcept
    -> void;
