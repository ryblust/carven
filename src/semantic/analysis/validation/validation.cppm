module carven:semantic.analysis.validation;

import :semantic.analysis.analyzer;
import :semantic.visibility;
import std;

auto diagnose_type_contracts(
    const SemanticConstruction& builder,
    DiagnosticSink& diagnostics
) noexcept -> void;

auto diagnose_declaration_surface_type(
    const SemanticConstruction& builder,
    DiagnosticSink& diagnostics,
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    HIRTypeID type,
    ProgramOriginID origin,
    std::string_view surface
) noexcept -> void;

auto diagnose_declaration_surface_constant(
    const SemanticConstruction& builder,
    DiagnosticSink& diagnostics,
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    HIRConstantID constant,
    ProgramOriginID origin,
    std::string_view surface
) noexcept -> void;
