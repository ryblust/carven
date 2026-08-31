module carven:semantic.analysis.availability;

import :semantic.analysis.analyzer;
import :semantic.analysis.session.read;

auto diagnose_availability(SemanticDraftView builder, DiagnosticSink& diagnostics) noexcept -> void;
