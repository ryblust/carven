module carven:semantic.analysis.effects;

import :semantic.analysis.analyzer;
import :semantic.analysis.control;
import :semantic.analysis.session.read;

auto diagnose_effects(
    SemanticDraftView builder,
    const CallableConstraints& callable_constraints,
    DiagnosticSink& diagnostics,
    const RecordedControlAnalysis& control
) noexcept -> void;
