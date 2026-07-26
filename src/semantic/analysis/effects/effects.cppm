module carven:semantic.analysis.effects;

import :semantic.analysis.analyzer;
import :semantic.analysis.control;

auto diagnose_effects(
    const SemanticConstruction& builder,
    const CallableConstraints& callable_constraints,
    DiagnosticSink& diagnostics,
    const SolvedControl& control
) noexcept -> void;
