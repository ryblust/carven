module carven:semantic.analysis.interop;

import :frontend.ast.decl;
import :frontend.ast.tree;
import :semantic.analysis.catalog;
import :semantic.analysis.diagnostics;
import :semantic.semir.program;
import std;

auto validate_cpp_boundary_declaration(
    ProgramDraft& draft,
    ProgramModuleID module,
    ASTView syntax,
    const ASTFunctionDecl& function,
    std::span<const ConstructionCallableParameter> parameters,
    ConstructionTypeRef result
) noexcept -> AnalysisResult<void>;

auto diagnose_cpp_api_surface(ProgramDraft& draft, AnalysisCatalogView catalog) noexcept
    -> AnalysisResult<void>;
