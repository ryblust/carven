module carven:semantic.analysis.storage_order;

import :diagnostics.sink;
import :semantic.analysis.builder;

auto diagnose_nominal_storage(
    SemanticConstruction& construction,
    DiagnosticSink& diagnostics
) noexcept -> void;
