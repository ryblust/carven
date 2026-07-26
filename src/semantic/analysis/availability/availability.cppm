module carven:semantic.analysis.availability;

import :semantic.analysis.analyzer;
import :semantic.analysis.control;

auto diagnose_availability(
    const SemanticConstruction& builder,
    DiagnosticSink& diagnostics,
    const SolvedControl& control
) noexcept -> void;
