module carven:semantic.analysis.interop;

import :diagnostics.sink;
import :frontend.ast.decl;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.session.read;
import :semantic.hir.type;
import std;

auto diagnose_cpp_boundary_declaration(
    ModuleAnalysis& module_analysis,
    const ASTFunctionDecl& function,
    std::span<const HIRFunctionParameterType> parameters,
    HIRTypeID result
) noexcept -> void;

auto diagnose_cpp_api_surface(SemanticDraftView semantic, DiagnosticSink& diagnostics) noexcept
    -> void;
