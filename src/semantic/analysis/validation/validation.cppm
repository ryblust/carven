module carven:semantic.analysis.validation;

import :semantic.analysis.analyzer;
import :semantic.analysis.session.read;
import :semantic.visibility;
import std;

auto diagnose_type_contracts(SemanticDraftView builder, DiagnosticSink& diagnostics) noexcept
    -> void;

auto diagnose_declaration_surface_type(
    SemanticDraftView builder,
    DiagnosticSink& diagnostics,
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    HIRTypeID type,
    ProgramOriginID origin,
    std::string_view surface
) noexcept -> void;

auto diagnose_declaration_surface_constant(
    SemanticDraftView builder,
    DiagnosticSink& diagnostics,
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    HIRConstantID constant,
    ProgramOriginID origin,
    std::string_view surface
) noexcept -> void;
