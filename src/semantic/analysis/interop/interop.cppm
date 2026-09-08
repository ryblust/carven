module carven:semantic.analysis.interop;

import :frontend.ast.decl;
import :frontend.ast.tree;
import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.program;
import std;

auto validate_cpp_boundary_head(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    const ASTFunctionDecl& function,
    std::span<const ConstructionCallableParameter> parameters
) noexcept -> AnalysisResult<void>;

auto validate_cpp_boundary_result(
    const ProgramDraft& draft,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTFunctionDecl& function,
    ConstructionTypeRef result
) noexcept -> AnalysisResult<void>;

auto diagnose_cpp_api_surface(ProgramDraft& draft, AnalysisCatalogView catalog) noexcept
    -> AnalysisResult<void>;
